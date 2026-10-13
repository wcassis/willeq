#!/usr/bin/env python3
"""Offline regression for real action coordinates and marker-calibrated video."""
from pathlib import Path
import tempfile
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from oracle_video import action_point, calibrate_video


def main():
    assert action_point('click', '26,615') == (26, 585)
    assert action_point('hold', '148,348') == (148, 318)
    assert action_point('open', 'character-create') == (123, 392)
    assert action_point('open', 'hotbar') is None
    assert action_point('random', '') == (148, 318)
    assert action_point('accept', '') == (50, 489)
    assert action_point('cancel', '') == (50, 509)
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp)
        font = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi',
                        '-i', 'color=c=black:s=1280x800:r=10:d=6',
                        '-vf', "drawbox=x=400:y=320:w=420:h=80:color=white:t=fill:enable='between(t,2,4)',"
                               f"drawtext=fontfile={font}:text='ORACLE TIMING MARKER':"
                               "fontsize=24:fontcolor=black:x=410:y=345:enable='between(t,2,4)'",
                        '-c:v', 'libx264', '-pix_fmt', 'yuv420p', str(out / 'video.mp4')], check=True)
        (out / 'video.mp4.t0').write_text('100.0\n')
        (out / 'video.marker.epoch').write_text('102.0\n')
        actual = calibrate_video(out)
        assert abs(actual - 100) <= 0.11, actual
        assert (out / 'video.calibration.tsv').is_file()
    print('PASS: marker-calibrated t0 and root-to-video action points')


if __name__ == '__main__':
    main()
