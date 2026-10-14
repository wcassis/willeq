"""Wall-clock/video alignment and root-screen action geometry for oracle reports."""
from pathlib import Path
import subprocess


def calibrate_video(out: Path) -> float:
    launch_t0 = float((out / 'video.mp4.t0').read_text())
    marker_epoch = float((out / 'video.marker.epoch').read_text())
    marker_frames = out / 'marker_frames'
    marker_frames.mkdir(exist_ok=True)
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(out / 'video.mp4'),
                    '-t', '10', '-vf', 'fps=10,crop=500:120:380:300',
                    str(marker_frames / 'f%04d.png')], check=True)
    marker_frame = None
    for frame in sorted(marker_frames.glob('f*.png')):
        ocr = subprocess.run(['tesseract', str(frame), 'stdout', '--psm', '6'],
                             capture_output=True, text=True, check=True).stdout.upper()
        if 'TIMING MARKER' in ocr:
            marker_frame = int(frame.stem[1:]) - 1
            break
    if marker_frame is None:
        raise RuntimeError('visible timeline BEGIN marker absent from video')
    t0 = marker_epoch - marker_frame / 10.0
    if not 0 <= t0 - launch_t0 <= 5:
        raise RuntimeError(f'video timestamp calibration outside startup bound: {t0-launch_t0:.3f}s')
    (out / 'video.calibration.tsv').write_text(
        f'launch_epoch\tmarker_epoch\tmarker_frame\tcalibrated_t0\n'
        f'{launch_t0:.6f}\t{marker_epoch:.6f}\t{marker_frame}\t{t0:.6f}\n')
    return t0


def action_point(verb: str, target: str):
    """Captured video has root-screen x, but starts 30 pixels below root y."""
    if verb in ('click', 'hold'):
        x, y = map(int, target.split(','))
        return x, y - 30
    if verb == 'open':
        # Hotbar/inventory use keys and have no action pointer to measure.
        return {'character-create': (123, 392)}.get(target)
    return {'random': (148, 318), 'accept': (50, 489),
            'cancel': (50, 509)}.get(verb)
