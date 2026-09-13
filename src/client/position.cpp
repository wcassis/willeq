#include "client/position.h"

#include <string>
#include <cmath>
#include <fmt/format.h>

std::string to_string(const glm::vec4 &position)
{
	return fmt::format(
		"({:.3f}, {:.3f}, {:.3f}, {:.3f})",
		position.x,
		position.y,
		position.z,
		position.w
	);
}

std::string to_string(const glm::vec3 &position)
{
	return fmt::format(
		"({:.3f}, {:.3f}, {:.3f})",
		position.x,
		position.y,
		position.z
	);
}

std::string to_string(const glm::vec2 &position)
{
	return fmt::format(
		"({:.3f}, {:.3f})",
		position.x,
		position.y
	);
}

bool IsOrigin(const glm::vec3 &position) {
	return glm::dot(position, position) == 0;
}

bool IsOrigin(const glm::vec4 &position) {
	return IsOrigin(glm::vec3(position));
}

/**
* Produces the non square root'ed distance between the two points within the XY plane.
*/
float DistanceSquared(const glm::vec2& point1, const glm::vec2& point2) {
	auto diff = point1 - point2;
	return glm::dot(diff, diff);
}

/**
* Produces the distance between the two points on the XY plane.
*/
float Distance(const glm::vec2& point1, const glm::vec2& point2) {
	return std::sqrt(DistanceSquared(point1, point2));
}

/**
* Produces the non square root'ed distance between the two points.
*/
float DistanceSquared(const glm::vec3& point1, const glm::vec3& point2) {
	auto diff = point1 - point2;
	return glm::dot(diff, diff);
}

/**
* Produces the non square root'ed distance between the two points.
*/
float DistanceSquared(const glm::vec4& point1, const glm::vec4& point2) {
	return DistanceSquared(static_cast<glm::vec3>(point1), static_cast<glm::vec3>(point2));
}

/**
* Produces the distance between the two points.
*/
float Distance(const glm::vec3& point1, const glm::vec3& point2) {
	return std::sqrt(DistanceSquared(point1, point2));
}

/**
* Produces the distance between the two points.
*/
float Distance(const glm::vec4& point1, const glm::vec4& point2) {
	return Distance(static_cast<glm::vec3>(point1), static_cast<glm::vec3>(point2));
}

/**
* Gives the heading directly 180 degrees from the
* current heading.
* Takes the EQfloat from the glm::vec4 and returns
* an EQFloat.
*/
float GetReciprocalHeading(const glm::vec4& point1) {
	return GetReciprocalHeading(point1.w);
}

/**
* Gives the heading directly 180 degrees from the
* current heading.
* Takes an EQfloat and returns an EQFloat.
*/
float GetReciprocalHeading(const float heading)
{
	float result;

	// Convert to radians
	const float h = (heading / 512.0f) * 6.283184f;

	// Calculate the reciprocal heading in radians
	result = h + 3.141592f;

	// Convert back to eq heading from radians
	result = (result / 6.283184f) * 512.0f;

	return result;
}
