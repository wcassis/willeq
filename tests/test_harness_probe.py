import pytest

from tools import harness_probe


def test_chebyshev_distance_1d_multiple_values():
    assert harness_probe.chebyshev_distance_1d((0, 5, 10), 4) == 6


def test_chebyshev_distance_1d_single_value():
    assert harness_probe.chebyshev_distance_1d((3,), 3) == 0


def test_chebyshev_distance_1d_empty_values():
    with pytest.raises(ValueError):
        harness_probe.chebyshev_distance_1d((), 0)
