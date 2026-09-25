def chebyshev_distance_1d(values: tuple, point: float) -> int:
    """Return the maximum absolute distance as an int; reject empty values."""
    if not values:
        raise ValueError("values must not be empty")
    return int(max(abs(value - point) for value in values))
