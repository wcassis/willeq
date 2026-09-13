#pragma once

#include <string>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/geometric.hpp>
#include "common/util/types.h"

std::string to_string(const glm::vec4 &position);
std::string to_string(const glm::vec3 &position);
std::string to_string(const glm::vec2 &position);

bool IsOrigin(const glm::vec3 &position);
bool IsOrigin(const glm::vec4 &position);

float DistanceSquared(const glm::vec2& point1, const glm::vec2& point2);
float Distance(const glm::vec2& point1, const glm::vec2& point2);
float DistanceSquared(const glm::vec3& point1, const glm::vec3& point2);
float Distance(const glm::vec3& point1, const glm::vec3& point2);

float DistanceSquared(const glm::vec4& point1, const glm::vec4& point2);
float Distance(const glm::vec4& point1, const glm::vec4& point2);

float GetReciprocalHeading(const glm::vec4& point1);
float GetReciprocalHeading(const float heading);
