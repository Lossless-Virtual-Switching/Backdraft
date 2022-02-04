// -*- c++ -*-

#include <mutex>

// #include "config.h"

#include "Generator.h"

Generator* createFacebookKey() { return new GEV(30.7984, 8.20449, 0.078688); }

Generator* createFacebookValue() {
  Generator* g = new GPareto(15.0, 214.476, 0.348238);

  Discrete* d = new Discrete(g);
  d->add(0.00536, 0.0);
  d->add(0.00047, 1.0);
  d->add(0.17820, 2.0);
  d->add(0.09239, 3.0);
  d->add(0.00018, 4.0);
  d->add(0.02740, 5.0);
  d->add(0.00065, 6.0);
  d->add(0.00606, 7.0);
  d->add(0.00023, 8.0);
  d->add(0.00837, 9.0);
  d->add(0.00837, 10.0);
  d->add(0.08989, 11.0);
  d->add(0.00092, 12.0);
  d->add(0.00326, 13.0);
  d->add(0.01980, 14.0);

  return d;
}

Generator* createFacebookIA() { return new GPareto(0, 16.0292, 0.154971); }

char *parse_generator_string(std::string str, char **r1, char **r2, char **r3) {
  char *s_copy = new char[str.length() + 1];
  strcpy(s_copy, str.c_str());
  char *saveptr = NULL;

  char *t_ptr = strtok_r(s_copy, ":", &saveptr);
  char *a_ptr = strtok_r(NULL, ":", &saveptr);

  if (t_ptr == NULL) // || a_ptr == NULL)
    DIE("strtok(.., \":\") failed to parse %s", str.c_str());

  saveptr = NULL;
  char *s1 = NULL;
  char *s2 = NULL;
  char *s3 = NULL;

  if (a_ptr) {
    s1 = strtok_r(a_ptr, ",", &saveptr);
    s2 = strtok_r(NULL, ",", &saveptr);
    s3 = strtok_r(NULL, ",", &saveptr);
  }

  *r1 = s1;
  *r2 = s2;
  *r3 = s3;

  return s_copy;
}

Generator* createGenerator(std::string str) {
  if (!strcmp(str.c_str(), "fb_key")) return createFacebookKey();
  else if (!strcmp(str.c_str(), "fb_value")) return createFacebookValue();
  else if (!strcmp(str.c_str(), "fb_ia")) return createFacebookIA();

  char *s1, *s2, *s3;
  char *s_copy = parse_generator_string(str, &s1, &s2, &s3);
  double a1 = s1 ? atof(s1) : 0.0;
  double a2 = s2 ? atof(s2) : 0.0;
  double a3 = s3 ? atof(s3) : 0.0;

  if (atoi(s_copy) != 0 || !strcmp(s_copy, "0")) {
    double v = atof(s_copy);
    delete[] s_copy;
    return new Fixed(v);
  }

  if      (strcasestr(str.c_str(), "fixed")) return new Fixed(a1);
  else if (strcasestr(str.c_str(), "normal")) return new Normal(a1, a2);
  else if (strcasestr(str.c_str(), "exponential")) return new Exponential(a1);
  else if (strcasestr(str.c_str(), "pareto")) return new GPareto(a1, a2, a3);
  else if (strcasestr(str.c_str(), "gev")) return new GEV(a1, a2, a3);
  else if (strcasestr(str.c_str(), "uniform")) return new Uniform(a1);
  else if (strcasestr(str.c_str(), "bimodal")) return new Bimodal(a1, a2, a3);
  else if (strcasestr(str.c_str(), "file")) return new FileGenerator(s1);
  else if (strcasestr(str.c_str(), "lognorm")) return new LogNormal(a1, a2);
  DIE("Unable to create Generator '%s'", str.c_str());

  delete[] s_copy;

  return NULL;
}

static Generator *_createPopularityGenerator(std::string str, long records, long permutation_seed) {
  Generator *ret = NULL;
  char *s1, *s2, *s3;
  char *s_copy = parse_generator_string(str, &s1, &s2, &s3);
  double a1 = s1 ? atof(s1) : 0.0;

  if      (strcasestr(str.c_str(), "uniform")) ret = new Uniform(records);
  else if (strcasestr(str.c_str(), "zipf")) ret = new Zipf(records, a1, permutation_seed);
  else DIE("Unable to create Request Generator '%s'", str.c_str());

  delete[] s_copy;

  return ret;
}

static std::mutex createPopGenLock;
static Generator *popularityGenerator;

Generator *createPopularityGenerator(std::string str, long records, long permutation_seed) {
  std::lock_guard<std::mutex> lock(createPopGenLock);
  if (!popularityGenerator) {
    popularityGenerator = _createPopularityGenerator(str, records, permutation_seed);
  }
  return popularityGenerator;
}

void deleteGenerator(Generator* gen) {
  delete gen;
}

#if TEST

// g++ -DTEST -std=c++11 Generator.cc && ./a.out bimodal:0.1,1,10 50

#include <iostream>

void log_file_line(log_level_t, char const*, int, char const*, ...)
{
}

int main(int argc, char **argv)
{
  int count = atoi(argv[2]);
  srand48(time(NULL));
  Generator *g = createGenerator(argv[1]);
  for (int i = 0; i < count; i++)
    std::cout << g->generate() << std::endl;
}
#endif
