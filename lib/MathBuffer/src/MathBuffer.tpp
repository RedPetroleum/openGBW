#include "MathBuffer.h"

template<typename T, size_t S>
constexpr MathBuffer<T,S>::MathBuffer() :
		headIndex(0), count(0) {
  static_assert(std::is_arithmetic<T>::value, "T must be numeric");
}

template<typename T,size_t S>
bool MathBuffer<T, S>::push(T value) {
  headIndex += 1;
  if (headIndex >= S) {
    headIndex = 0;
  }
  if (count < S) {
    count += 1;
  }

  buffer[headIndex] = value;
  bufferTimestamp[headIndex] = millis();

  return count == S; // Return true if buffer is full
}

template<typename T,size_t S>
void MathBuffer<T, S>::executeOnSamplesSince(int64_t cutoffMs, std::function<void (T, int64_t)> iterator) {
  for (int i = 0; i < count; i++) {
    int index = (headIndex - i); // going backward to go from newest to oldest
    if (index < 0) { // wrap around
      index += S;
    }
    if (bufferTimestamp[index] < cutoffMs) {
      return;
    }
    iterator(buffer[index], bufferTimestamp[index]);
  }
}

template<typename T,size_t S>
size_t MathBuffer<T, S>::countSamplesSince(int64_t cutoffMs) {
  for (int i = 0; i < count; i++) {
    int index = (headIndex - i); // going backward to go from newest to oldest
    if (index < 0) { // wrap around
      index += S;
    }
    if (bufferTimestamp[index] < cutoffMs) {
      return i;
    }
  }

  return count;
}


template<typename T,size_t S>
T MathBuffer<T, S>::averageSince(int64_t cutoffMs) {
  size_t sampleCount = countSamplesSince(cutoffMs);

  T average = 0;
  executeOnSamplesSince(cutoffMs, [&average, &sampleCount](T value, int64_t ms) {
    average += value / sampleCount;
  });

  return average;
}

template<typename T,size_t S>
T MathBuffer<T, S>::maxSince(int64_t cutoffMs) {
  T max = 0;
  bool isFirst = true;

  executeOnSamplesSince(cutoffMs, [&max, &isFirst](T value, int64_t ms) {
    if (isFirst || value > max) {
      max = value;
      isFirst = false;
    }
  });

  return max;
}

template<typename T,size_t S>
T MathBuffer<T, S>::minSince(int64_t cutoffMs) {
  T min = 0;
  bool isFirst = true;

  executeOnSamplesSince(cutoffMs, [&min, &isFirst](T value, int64_t ms) {
    if (isFirst || value < min) {
      min = value;
      isFirst = false;
    }
  });

  return min;
}

template<typename T,size_t S>
T MathBuffer<T, S>::firstValueOlderThan(int64_t cutoffMs) {
  for (int i = 0; i < count; i++) {
    int index = (headIndex - i); // going backward to go from newest to oldest
    if (index < 0) { // wrap around
      index += S;
    }
    if (bufferTimestamp[index] < cutoffMs) {
      return buffer[index];
    }
  }
  return 0;
}

// The lowest and the highest of the last n samples,
// false if there are not that many samples yet
template<typename T,size_t S>
bool MathBuffer<T, S>::spreadOfLast(size_t n, T &lowest, T &highest) {
  if (n == 0 || count < n) {
    return false;
  }

  for (int i = 0; i < (int)n; i++) {
    int index = (headIndex - i); // going backward to go from newest to oldest
    if (index < 0) { // wrap around
      index += S;
    }
    if (i == 0 || buffer[index] < lowest) {
      lowest = buffer[index];
    }
    if (i == 0 || buffer[index] > highest) {
      highest = buffer[index];
    }
  }

  return true;
}

// The average of the last n samples, 0 if there are not that many samples yet
template<typename T,size_t S>
T MathBuffer<T, S>::averageOfLast(size_t n) {
  if (n == 0 || count < n) {
    return 0;
  }

  T sum = 0;
  for (int i = 0; i < (int)n; i++) {
    int index = (headIndex - i); // going backward to go from newest to oldest
    if (index < 0) { // wrap around
      index += S;
    }
    sum += buffer[index];
  }

  return sum / (T)n;
}

// The average of the last n samples with the two oldest of them counted with less weight:
// the oldest with `oldest`, the one after it with `second`, every other one with 1. Reaching
// the edge of a settled stretch, the oldest samples still carry a little of the movement before
// it, and they are meant to say less about the value than the newest ones.
// 0 if there are not that many samples yet, and with two or fewer the weights are all there is,
// so those are taken plain
template<typename T,size_t S>
T MathBuffer<T, S>::taperedAverageOfLast(size_t n, T oldest, T second) {
  if (n == 0 || count < n) {
    return 0;
  }
  if (n <= 2) {
    return averageOfLast(n);
  }

  T sum = 0, weights = 0;
  for (int i = 0; i < (int)n; i++) {
    int index = (headIndex - i); // going backward to go from newest to oldest
    if (index < 0) { // wrap around
      index += S;
    }
    T weight = i == (int)n - 1 ? oldest : (i == (int)n - 2 ? second : 1);
    sum += buffer[index] * weight;
    weights += weight;
  }

  return sum / weights;
}

// The same taper over everything newer than cutoffMs, see taperedAverageOfLast().
// How many samples that is is only known once they have been walked, so they are all summed
// with a weight of 1 and the two oldest are taken back out of the sum afterwards
template<typename T,size_t S>
T MathBuffer<T, S>::taperedAverageSince(int64_t cutoffMs, T oldest, T second) {
  T sum = 0, last = 0, secondLast = 0;
  size_t seen = 0;
  executeOnSamplesSince(cutoffMs, [&](T value, int64_t ms) {
    secondLast = last; // walked newest to oldest, so these two end up being the oldest two
    last = value;
    sum += value;
    seen++;
  });

  if (seen == 0) {
    return 0;
  }
  if (seen <= 2) {
    return sum / (T)seen; // with two or fewer the weights are all there is
  }
  return (sum - (1 - oldest) * last - (1 - second) * secondLast)
         / ((T)seen - (1 - oldest) - (1 - second));
}

// True if the last n samples all lie within tolerance of each other,
// false if there are not that many samples yet
template<typename T,size_t S>
bool MathBuffer<T, S>::isSteady(size_t n, T tolerance) {
  T lowest = 0, highest = 0;
  return spreadOfLast(n, lowest, highest) && highest - lowest <= tolerance;
}

// When the oldest of the last n readings was taken, 0 if there are not that many yet. The caller that
// asked a question about those n readings gets the moment they reach back to out of it
template<typename T,size_t S>
int64_t MathBuffer<T, S>::timestampOfLast(size_t n) {
  if (n == 0 || n > count) {
    return 0;
  }
  int index = (int)headIndex - (int)(n - 1);
  if (index < 0) { // wrap around
    index += S;
  }
  return bufferTimestamp[index];
}

// Macro to calculate the absolute value
#define ABS(a) (((a) > 0) ? (a) : ((a) * -1))
