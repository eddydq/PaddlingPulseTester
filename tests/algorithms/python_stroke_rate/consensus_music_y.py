from common import estimate_consensus_music_stroke_rate

ALGORITHM_NAME = "consensus_music_y"


def calculate(snapshot: dict[str, object]) -> float:
    series = snapshot["series"]
    return estimate_consensus_music_stroke_rate(series["y"])
