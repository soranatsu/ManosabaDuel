"""Verify the reviewed combat bank without installing audio packages."""

import array
import hashlib
import json
import math
from pathlib import Path
import re
import sys
import wave
import zlib


root = Path(__file__).resolve().parent
manifest = json.loads((root / "COMBAT_VOICE_PROVENANCE346.json").read_text(encoding="utf-8"))
source = (root.parents[1] / "Source" / "ManosabaDuel" / "DuelVoice.cpp").read_text(encoding="utf-8-sig")
assert len(manifest["clips"]) == 23
assert sum(c["variant"] == "Primary" for c in manifest["clips"]) == 18
assert sum(c["variant"] == "Alternate" for c in manifest["clips"]) == 5
assert len({(c["character"], c["cue"], c["variant"]) for c in manifest["clips"]}) == 23
checksums = set()
duration = 0.0
for clip in manifest["clips"]:
    assert clip.get("id") != "0203Adv14_Hiro016" and clip["pcm_crc32"] != "46145fb8", "Retired Hiro shout is active"
    path = root / clip["file"]
    assert hashlib.sha256(path.read_bytes()).hexdigest() == clip["sha256"], path.name
    original = root / clip["original_file"]
    expected_original = clip.get("original_sha256", clip.get("source_sha256"))
    assert hashlib.sha256(original.read_bytes()).hexdigest() == expected_original, original.name
    with wave.open(str(path)) as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 2, 24000), path.name
        pcm = wav.readframes(wav.getnframes())
        seconds = wav.getnframes() / wav.getframerate()
    crc = f"{zlib.crc32(pcm) & 0xffffffff:08x}"
    assert crc == clip["pcm_crc32"], path.name
    checksums.add(crc)
    assert math.isclose(seconds, clip["duration_seconds"], abs_tol=1 / 24000), path.name
    assert 0.1 <= seconds <= (3 if clip["cue"] in ("Attack", "Art", "Hurt") else 10), path.name
    pattern = rf'TEXT\("{re.escape(path.name)}"\)[^\n]*0x{crc}'
    assert re.search(pattern, source, re.IGNORECASE), f"Runtime checksum differs: {path.name}"
    samples = array.array("h", pcm)
    if sys.byteorder != "little":
        samples.byteswap()
    rms = math.sqrt(sum(value * value for value in samples) / len(samples))
    assert rms > 26 and max(abs(value) for value in samples) > 96, f"Inaudible source: {path.name}"
    duration += seconds
assert len(checksums) == 23
north = source.split("constexpr FVoiceSources NorthSources", 1)[1].split("static_assert", 1)[0]
assert 'TEXT("North_Hiro_Art.wav")' not in north and "0x46145fb8" not in north
assert "我不会放弃你" not in north
print(f"PASS: 23 distinct recordings, 18 primary slots + 5 boss alternates, {duration:.2f} seconds; originals, WAV/PCM hashes, runtime checksums, retired-shout exclusion and duration limits verified.")
