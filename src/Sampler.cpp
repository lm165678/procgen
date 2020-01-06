#include <glm/glm.hpp>
#include <noise/noise.h>
#include <algorithm>
#include <future>
#include <thread>
#include <assert.h>
#include <vector_types.h>

#include "Sampler.h"

#include "NoiseSampler.cuh"

namespace Sampler
{

int index3D(int x, int y, int z, int dimensionLength)
{
	return x + dimensionLength * (y + dimensionLength * z);
}
int index3D(glm::ivec3 coord, int dimensionLength)
{
	return index3D(coord.x, coord.y, coord.z, dimensionLength);
}

// integer as floats only exact up to 2^24 = 16,777,216
// shouldn't be a problem?
int index3DCuda(glm::vec3 coord, int dimensionLength)
{
	glm::vec3 expandedCoord(coord.x * 8.0, coord.y * 8.0, coord.z * 8.0);
	int expandedDimensionLength = dimensionLength * 8;
	
	// TODO: check that results are always exact integers
	float result = (expandedCoord.x + expandedDimensionLength * (expandedCoord.y + expandedDimensionLength * expandedCoord.z));
	//printf("index at (%f, %f, %f) is = %f\n", coord.x, coord.y, coord.z, result);

	return result;
}

float mod(float x, float y)
{
	return x - y * floor(x / y);
}
float repeatAxis(float p, float c)
{
	return mod(p, c) - 0.5f * c;
}
float Sphere(const glm::vec3& worldPosition, const glm::vec3& origin, float radius)
{
	return glm::length(worldPosition - origin) - radius; // non repeating
}

float Box(const glm::vec3& p, const glm::vec3& size)
{
	glm::vec3 d = abs(p) - size;
	glm::vec3 maxed(0);
	for (int i = 0; i < 3; i++)
	{
		maxed[i] = std::max(d[i], 0.f);
	}
	return glm::length(maxed) + std::min(std::max(d.x, std::max(d.y, d.z)), 0.f);
}

float Noise(const glm::vec3& p, noise::module::Perlin& noiseModule)
{
	double epsilon = 0.500f;
	float divider = 13.f;
	float value = (float)noiseModule.GetValue(p.x / divider  + epsilon, p.y / divider + epsilon, p.z / divider + epsilon);

	divider = 100.f;
	value += (float)noiseModule.GetValue(p.x / divider  + epsilon, p.y / divider + epsilon, p.z / divider + epsilon);

	divider = 400.f;
	value += (float)noiseModule.GetValue(p.x / divider  + epsilon, p.y / divider + epsilon, p.z / divider + epsilon);

	//if (p.y > 20.f)
	//{
	//	divider = 200.f;
	//	value -= (float)noiseModule.GetValue(p.x / divider  + epsilon, p.y / divider + epsilon, p.z / divider + epsilon);
	//	value -= 1.0f;
	//}
	//if (p.y > 40.f)
	//{
	//	value += 1.5f;
	//}
	return value;
}

float Waves(const glm::vec3& p)
{
	//printf("density at (%f, %f, %f) is = %f\n", p.x, p.y, p.z, value);
	//std::cout << "position: " << p.x << std::endl;
	//printf("position %f %f %f\n", p.x, p.y, p.z);
	//return sin(p.x * 1.0) + cos(p.y * 1.0) + p.z - 2;

	//float value = sin(p.x * 1.0f) / 1.f + cos(p.y * 1.0f) / 1.f + p.z - 5.50f;
	float value = sin(p.x * 0.5f) / 0.3f + p.y - 5.50f;

	//return sin(p.x) + cos(p.z) + p.y;
	return value;
}

float Plane(const glm::vec3& p)
{
	return p.x - 0.00001f *p.y;
}

// Size parameter instead of max coordinate to enforce same size in all coordinate axes for easier indexing
std::vector<float> AsyncCache(glm::ivec3 min, int segmentStart, int sampleCount, int size)
{
	noise::module::Perlin noiseModule;
	std::vector<float> samples(sampleCount);

	for (int i = 0; i < sampleCount; i++)
	{
		int idx = i + segmentStart;
		int x = idx % size;
		int y = (idx / size) % size;
		int z = idx / (size * size);
		samples[i] = Noise(min + glm::ivec3(x, y, z), noiseModule);
	}
	return samples;
}

std::vector<float4> BuildCacheCuda(const glm::ivec3 min, const unsigned size)
{
	const int samplesPerUnit = 8;
	const int samplesPerDirection = 8 * size;
	const int numSamples = samplesPerDirection * samplesPerDirection * samplesPerDirection;

	std::vector<float4> results;
	float4* res = CudaNoise::CacheArea(min.x, min.y, min.z, size);
	results.assign(res, res + numSamples);
	return results;
}

std::vector<std::vector<float>> BuildCache(const glm::ivec3 min, const unsigned size)
{
	unsigned idxCount = size * size * size;
	unsigned concurrentThreadsSupported = std::thread::hardware_concurrency();

	// perf-wise no sense in computing less than thousands of samples per thread, but for debugging it's nice
	unsigned threads = std::min(concurrentThreadsSupported, idxCount / 8); 

	unsigned samplesPerSegment = idxCount / threads;

	unsigned extras = size % threads;
	//assert(size % threads == 0);

	std::vector<std::future<std::vector<float>>> futureSamples;
	std::vector<std::vector<float>> results;
	for (unsigned i = 0; i < threads; i++)
	{
		int segmentStart = i * (idxCount / threads);
		futureSamples.push_back(std::async(
			std::launch::async,
			AsyncCache,
			min,
			segmentStart,
			i < threads-1 ? samplesPerSegment : samplesPerSegment + extras,
			size));
	}

	for (auto& future : futureSamples)
	{
		results.push_back(future.get());
	}

	return results;
}

float4 SampleCacheCuda(const std::vector<float4>& cache, const int size, const glm::vec3 coordinate, const glm::ivec3 min)
{
	// Take into account 8 per unit
	const glm::vec3 innerCoordinate(coordinate.x - min.x, coordinate.y - min.y, coordinate.z - min.z);
	const unsigned long idx = index3DCuda(innerCoordinate, size);
	return cache[idx];
}

// Get sample from cache
float SampleCache(const std::vector<std::vector<float>>& cache, const int coordinate)
{
	const int cacheCount = cache.size();
	const int cacheLength = cache[0].size();
	int cacheIdx = coordinate / cacheLength;
	cacheIdx = cacheIdx >= cacheCount ? cacheCount - 1 : cacheIdx;
	const int idx = coordinate % cacheLength;
	return cache[cacheIdx][idx];
}

// Get sample from cache
float SampleCache(const std::vector<std::vector<float>>& cache, const glm::ivec3 min, const int size, const glm::ivec3 coordinate)
{
	//printf("pos %i %i %i \n", coordinate.x, coordinate.y, coordinate.z);
	return SampleCache(cache, index3D(coordinate - min, size));
}

float Density(const glm::vec3 pos)
{
	static noise::module::Perlin noiseModule;
	//printf("density %f %f %f \n", pos.x, pos.y, pos.z);

	//glm::vec3 repeat(15, 15, 15);
	//glm::vec3 repeatPos(
	//	repeatAxis(pos.x, repeat.x),
	//	repeatAxis(pos.y, repeat.y),
	//	repeatAxis(pos.z, repeat.z)
	//);
	//return glm::length(pos - origin) - radius; // repeating
	return Noise(pos, noiseModule);
	//return Sphere(repeatPos, glm::vec3(0, 0, 0), 6.0);

	//return Box(pos - glm::vec3(16,16,16), glm::vec3(128, 8, 8));
	//return Box(repeatPos - glm::vec3(0,0,0), glm::vec3(5, 5, 5));

	//return Plane(pos);
	//return Waves(pos);
}

float DensityCuda(const std::vector<float4>& cache, const int size, const glm::vec3 pos, const glm::vec3 min)
{
	float4 sample = SampleCacheCuda(cache, size, pos, min);
	return sample.x;
}

float Sample(const glm::vec3 pos)
{
	float value = Density(pos);
	//printf("position (%f %f %f) value = %f \n", pos.x, pos.y, pos.z, value);
	//return value >= 0.0f;
	return value;
}
}
