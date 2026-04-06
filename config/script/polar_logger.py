"""Polar Verity Sense BLE ACC logger — replicates DA14531 firmware pipeline."""

from __future__ import annotations

import asyncio
import csv
import struct
import sys
from datetime import datetime
from pathlib import Path

from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

# ---------------------------------------------------------------------------
# Sample store — mirrors pp_sample_store (firmware/app/src/paddling_pulse_sample_store.c)
# ---------------------------------------------------------------------------
SAMPLE_STORE_CAPACITY = 512


class SampleStore:
    """Circular buffer of 512 x int16, identical to pp_sample_store."""

    def __init__(self):
        self.samples = [0] * SAMPLE_STORE_CAPACITY
        self.wr_idx = 0
        self.count = 0

    def push(self, sample: int) -> None:
        sample = max(-32768, min(32767, sample))
        self.samples[self.wr_idx] = sample
        self.wr_idx = (self.wr_idx + 1) % SAMPLE_STORE_CAPACITY
        if self.count < SAMPLE_STORE_CAPACITY:
            self.count += 1

    def get(self, ordered_index: int) -> int:
        if ordered_index >= self.count:
            return 0
        oldest = (self.wr_idx + SAMPLE_STORE_CAPACITY - self.count) % SAMPLE_STORE_CAPACITY
        idx = (oldest + ordered_index) % SAMPLE_STORE_CAPACITY
        return self.samples[idx]

    def snapshot(self) -> list[int]:
        return [self.get(i) for i in range(SAMPLE_STORE_CAPACITY)]


# ---------------------------------------------------------------------------
# PMD protocol constants
# ---------------------------------------------------------------------------
PMD_SERVICE_UUID = "fb005c80-02e7-f387-1cad-8acd2d8df0c8"
PMD_CP_UUID      = "fb005c81-02e7-f387-1cad-8acd2d8df0c8"
PMD_DATA_UUID    = "fb005c82-02e7-f387-1cad-8acd2d8df0c8"

OP_GET_SETTINGS = 0x01
OP_START_MEAS   = 0x02
OP_STOP_MEAS    = 0x03
MEAS_ACC        = 0x02
CP_RSP_CODE     = 0xF0
STATUS_SUCCESS  = 0x00

# Setting type IDs
SET_SAMPLE_RATE     = 0x00
SET_RESOLUTION      = 0x01
SET_RANGE           = 0x02
SET_RANGE_MILLIUNIT = 0x03
SET_CHANNELS        = 0x04

# Preferred settings (same as firmware lines 581-605)
PREFERRED_SETTINGS = {
    SET_SAMPLE_RATE: 52,
    SET_RESOLUTION: 16,
    SET_RANGE: 8,
    SET_CHANNELS: 3,
}

# Field sizes per setting type (firmware polar_setting_field_size)
SETTING_FIELD_SIZE = {
    SET_SAMPLE_RATE: 2,
    SET_RESOLUTION: 2,
    SET_RANGE: 2,
    SET_RANGE_MILLIUNIT: 4,
    SET_CHANNELS: 1,
}

PMD_DATA_HDR_LEN = 10

# Three stores — one per axis (firmware uses one, we log all three)
store_x = SampleStore()
store_y = SampleStore()
store_z = SampleStore()


def _store_sample(x: int, y: int, z: int) -> None:
    """Push one ACC sample to all three axis stores (clamped to int16)."""
    store_x.push(x)
    store_y.push(y)
    store_z.push(z)


def _parse_signed_le(data: bytes, offset: int, size: int) -> int:
    """Read a little-endian signed integer of 'size' bytes.
    Mirrors polar_parse_signed_le (firmware line 613)."""
    val = 0
    for i in range(size):
        val |= data[offset + i] << (8 * i)
    if size < 4 and (val & (1 << (8 * size - 1))):
        val |= ~((1 << (8 * size)) - 1)
    return val


def _parse_signed_bits(data: bytes, bit_offset: int, bit_width: int) -> int:
    """Read a bitpacked signed integer of 'bit_width' bits.
    Mirrors polar_parse_signed_bits (firmware line 625)."""
    val = 0
    for i in range(bit_width):
        byte_idx = (bit_offset + i) // 8
        bit_idx = (bit_offset + i) % 8
        if data[byte_idx] & (1 << bit_idx):
            val |= (1 << i)
    if bit_width < 32 and (val & (1 << (bit_width - 1))):
        val |= ~((1 << bit_width) - 1)
    return val


def _parse_raw_frame(frame_type: int, data: bytes) -> None:
    """Mirrors polar_parse_raw_frame (firmware line 656)."""
    bytes_per_val = frame_type + 1     # 0->1, 1->2, 2->3
    bytes_per_sample = bytes_per_val * 3
    sample_count = len(data) // bytes_per_sample

    for i in range(sample_count):
        off = i * bytes_per_sample
        x = _parse_signed_le(data, off, bytes_per_val)
        y = _parse_signed_le(data, off + bytes_per_val, bytes_per_val)
        z = _parse_signed_le(data, off + 2 * bytes_per_val, bytes_per_val)
        _store_sample(x, y, z)


def _parse_compressed_frame(data: bytes) -> None:
    """Mirrors polar_parse_compressed_frame (firmware line 674)."""
    if len(data) < 6:
        return

    # Initial absolute sample (3 x int16 LE)
    x = struct.unpack_from("<h", data, 0)[0]
    y = struct.unpack_from("<h", data, 2)[0]
    z = struct.unpack_from("<h", data, 4)[0]
    _store_sample(x, y, z)

    pos = 6
    while pos + 2 <= len(data):
        delta_size  = data[pos]       # bits per delta per axis
        block_count = data[pos + 1]   # samples in this block
        pos += 2

        bits_per_sample = delta_size * 3
        total_bits = block_count * bits_per_sample
        byte_count = (total_bits + 7) // 8

        if pos + byte_count > len(data):
            break

        bit_offset = pos * 8
        for _ in range(block_count):
            dx = _parse_signed_bits(data, bit_offset, delta_size)
            bit_offset += delta_size
            dy = _parse_signed_bits(data, bit_offset, delta_size)
            bit_offset += delta_size
            dz = _parse_signed_bits(data, bit_offset, delta_size)
            bit_offset += delta_size

            x += dx
            y += dy
            z += dz
            _store_sample(x, y, z)

        pos += byte_count


def handle_acc_notification(_sender, data: bytearray) -> None:
    """BLE notification callback — mirrors polar_parse_acc_packet (firmware line 718)."""
    if len(data) < PMD_DATA_HDR_LEN + 1:
        return
    if data[0] != MEAS_ACC:
        return

    frame_info = data[9]
    compressed = (frame_info & 0x80) != 0
    frame_type = frame_info & 0x7F

    payload = bytes(data[PMD_DATA_HDR_LEN:])

    if compressed:
        _parse_compressed_frame(payload)
    else:
        _parse_raw_frame(frame_type, payload)


# ---------------------------------------------------------------------------
# ACC settings negotiation — mirrors firmware lines 456-607
# ---------------------------------------------------------------------------

def parse_acc_settings(data: bytes) -> tuple[int, bytes]:
    """Parse GET_SETTINGS response, select preferred values, build TLV payload.

    Returns (sample_rate_hz, selected_tlv_bytes).
    Mirrors polar_parse_acc_settings (firmware line 526).
    """
    settings: dict[int, list[int]] = {}
    pos = 0

    while pos + 2 <= len(data):
        setting_type = data[pos]
        val_count = data[pos + 1]
        pos += 2
        field_size = SETTING_FIELD_SIZE.get(setting_type, 0)
        if field_size == 0 or pos + val_count * field_size > len(data):
            break

        values = []
        for _ in range(val_count):
            values.append(_parse_signed_le(data, pos, field_size))
            pos += field_size
        settings[setting_type] = values

    # Select preferred or first available (firmware polar_select_setting)
    selected_tlvs = bytearray()
    sample_rate_hz = 52  # default

    for setting_type in (SET_SAMPLE_RATE, SET_RESOLUTION, SET_RANGE, SET_CHANNELS):
        available = settings.get(setting_type)
        if not available:
            # Try RANGE_MILLIUNIT if RANGE not available
            if setting_type == SET_RANGE:
                available = settings.get(SET_RANGE_MILLIUNIT)
                if available:
                    setting_type = SET_RANGE_MILLIUNIT
            if not available:
                continue

        preferred = PREFERRED_SETTINGS.get(setting_type, -1)
        selected = preferred if preferred in available else available[0]

        if setting_type == SET_SAMPLE_RATE:
            sample_rate_hz = selected

        # Append TLV: type(1) + count(1) + value(field_size LE)
        field_size = SETTING_FIELD_SIZE[setting_type]
        selected_tlvs.append(setting_type)
        selected_tlvs.append(1)
        selected_tlvs.extend(selected.to_bytes(field_size, "little", signed=True))

    return sample_rate_hz, bytes(selected_tlvs)


# ---------------------------------------------------------------------------
# CSV logger
# ---------------------------------------------------------------------------
LOGS_DIR = Path(__file__).resolve().parent.parent / "logs"


def _next_log_path() -> Path:
    """Find next polar_log_NNN.csv filename."""
    LOGS_DIR.mkdir(parents=True, exist_ok=True)
    highest = 0
    for f in LOGS_DIR.glob("polar_log_*.csv"):
        try:
            n = int(f.stem.split("_")[-1])
            if n > highest:
                highest = n
        except ValueError:
            pass
    return LOGS_DIR / f"polar_log_{highest + 1:03d}.csv"


def _csv_header() -> list[str]:
    cols = ["timestamp", "count"]
    for axis in ("x", "y", "z"):
        cols.extend(f"{axis}_{i:03d}" for i in range(SAMPLE_STORE_CAPACITY))
    return cols


class CsvLogger:
    def __init__(self):
        self.path = _next_log_path()
        self.file = open(self.path, "w", newline="")
        self.writer = csv.writer(self.file)
        self.writer.writerow(_csv_header())
        self.file.flush()
        self.row_count = 0

    def write_snapshot(self) -> None:
        now = datetime.now().isoformat(timespec="milliseconds")
        count = store_x.count
        row = [now, count]
        row.extend(store_x.snapshot())
        row.extend(store_y.snapshot())
        row.extend(store_z.snapshot())
        self.writer.writerow(row)
        self.file.flush()
        self.row_count += 1

        # Console status
        latest_x = store_x.get(count - 1) if count > 0 else 0
        latest_y = store_y.get(count - 1) if count > 0 else 0
        latest_z = store_z.get(count - 1) if count > 0 else 0
        print(f"[{count}/{SAMPLE_STORE_CAPACITY}] x={latest_x} y={latest_y} z={latest_z}")

    def close(self) -> None:
        self.file.close()
