
#include <iostream>
#include <vector>
#include <map>
#include <random>
#include <iterator>
#include <algorithm>

class RandomUniform {
 public:
  RandomUniform() : device_{}, generator_{device_()} {}

  int operator()(const int max) {
    return operator()(0, max);
  }

  int operator()(const int min, const int max) {
    std::uniform_int_distribution<int> distribution{min, max};
    return distribution(generator_);
  }

 private:
  std::random_device device_;
  std::default_random_engine generator_;
};




auto CreateRandomVector(const size_t length,
                        const int random_min = 1, 
                        const int random_max = 9) {
  RandomUniform random{};
  std::vector<int> result{};
  auto generate =  [&random, random_min, random_max] { return random(random_min, random_max); };
  std::generate_n(std::back_inserter(result), length, generate);
  return result;
}

auto CreateRandomMap(const size_t length,
                     const int random_min = 1, 
                     const int random_max = 9) {
  RandomUniform random{};
  std::map<int, int> result{};
  auto generate = [&random, random_min, random_max] { static int i{-1}; ++i; return std::make_pair(i, random(random_min, random_max)); };
  std::generate_n(std::inserter(result, result.end()), length, generate);
  return result;
}

template<template<class...> class Cont, class T, class... Args>
void RandomlyRemoveElementsFrom(Cont<T, Args...>& container) {
  RandomUniform random{};
  const int number_of_removed_elements{random(5, 15)};
  for (int i{}; i < number_of_removed_elements; ++i) {
    const auto container_size{container.size()};
    if (container_size > 0) {
      RandomUniform random{};
      const int index{random(container_size - 1)};
      auto position{container.begin()};
      std::advance(position, index);
      container.erase(position);
    }
  }
}

void SynchronizeVectorWithMap(std::vector<int>& vector, std::map<int, int>& map) {
  auto predicate = [&map](const int e) {
    return map.end() == std::find_if(
      map.begin(), map.end(), [&e](const auto& p) { return p.second == e;}
    ); 
  };
  auto new_end{std::remove_if(vector.begin(), vector.end(), predicate)};
  vector.erase(new_end, vector.end());
}

void SynchronizeMapWithVector(std::map<int, int>& map, std::vector<int>& vector) {
  auto predicate{ [&vector](const auto& p) { return vector.end() == std::find(vector.begin(), vector.end(), p.second); } };
  auto iter{map.begin()};
  for(; iter != map.end(); ) {
    if (predicate(*iter)) {
      map.erase(iter++);
    } else {
      ++iter;
    }
  }
}

void SynchronizeContainers(std::vector<int>& vector, std::map<int, int>& map) {
  SynchronizeVectorWithMap(vector, map);
  SynchronizeMapWithVector(map, vector);
}

#ifdef DEBUG_
void PrintOutVector(const std::vector<int>& vector) {
  for (const auto e: vector) {
    std::cout << e << " "; 
  }
  std::cout << std::endl;
}

void PrintOutMap(const std::map<int, int>& map) {
  for (const auto& [key, value]: map) {
    std::cout << "[" << key << ", " << value << "], "; 
  }
  std::cout << std::endl;
}
#else
#define PrintOutVector(...)
#define PrintOutMap(...)
#endif

int main() {
  auto vector{CreateRandomVector(20)};
  auto map{CreateRandomMap(20)};
  
  PrintOutVector(vector);
  PrintOutMap(map);
  
  RandomlyRemoveElementsFrom(vector);
  RandomlyRemoveElementsFrom(map);

  PrintOutVector(vector);
  PrintOutMap(map);
    
  SynchronizeContainers(vector, map);
  
  PrintOutVector(vector);
  PrintOutMap(map);
}






