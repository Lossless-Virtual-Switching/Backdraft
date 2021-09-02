// -*- c++ -*-

// 1. implement "fixed" generator
// 2. implement discrete generator
// 3. implement combine generator? 

#ifndef GENERATOR_H
#define GENERATOR_H

#define MAX(a,b) ((a) > (b) ? (a) : (b))

// #include "config.h"

#include <fstream>
#include <string>
#include <vector>
#include <utility>

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "util.h"

// Generator syntax:
//
// \d+ == fixed
// n[ormal]:mean,sd
// e[xponential]:lambda
// p[areto]:scale,shape
// g[ev]:loc,scale,shape
// fb_value, fb_key, fb_rate

class Generator {
public:
  Generator() {}
  //  Generator(const Generator &g) = delete;
  //  virtual Generator& operator=(const Generator &g) = delete;
  virtual ~Generator() {}

  virtual double generate(double U = -1.0) = 0;
  virtual void set_lambda(double lambda) {DIE("set_lambda() not implemented");}
protected:
  std::string type;
};

class Fixed : public Generator {
public:
  Fixed(double _value = 1.0) : value(_value) { D("Fixed(%f)", value); }
  virtual double generate(double U = -1.0) { return value; }
  virtual void set_lambda(double lambda) {
    if (lambda > 0.0) value = 1.0 / lambda;
    else value = 0.0;
  }

private:
  double value;
};

class Uniform : public Generator {
public:
  Uniform(double _scale) : scale(_scale) { D("Uniform(%f)", scale); }

  virtual double generate(double U = -1.0) {
    if (U < 0.0) U = drand48();
    return scale * U;
  }

  virtual void set_lambda(double lambda) {
    if (lambda > 0.0) scale = 2.0 / lambda;
    else scale = 0.0;
  }

private:
  double scale;
};

class Normal : public Generator {
public:
  Normal(double _mean = 1.0, double _sd = 1.0) : mean(_mean), sd(_sd) {
    D("Normal(mean=%f, sd=%f)", mean, sd);
  }

  virtual double generate(double U = -1.0) {
    if (U < 0.0) U = drand48();
    double V = U; // drand48();
    double N = sqrt(-2 * log(U)) * cos(2 * M_PI * V);
    return mean + sd * N;
  }

  virtual void set_lambda(double lambda) {
    if (lambda > 0.0) mean = 1.0 / lambda;
    else mean = 0.0;
  }

private:
  double mean, sd;
};

class Exponential : public Generator {
public:
  Exponential(double _lambda = 1.0) : lambda(_lambda) {
    D("Exponential(lambda=%f)", lambda);
  }

  virtual double generate(double U = -1.0) {
    if (lambda <= 0.0) return 0.0;
    if (U < 0.0) U = drand48();
    return -log(U) / lambda;
  }

  virtual void set_lambda(double lambda) { this->lambda = lambda; }

private:
  double lambda;
};

class GPareto : public Generator {
public:
  GPareto(double _loc = 0.0, double _scale = 1.0, double _shape = 1.0) :
    loc(_loc), scale(_scale), shape(_shape) {
    assert(shape != 0.0);
    D("GPareto(loc=%f, scale=%f, shape=%f)", loc, scale, shape);
  }

  virtual double generate(double U = -1.0) {
    if (U < 0.0) U = drand48();
    return loc + scale * (pow(U, -shape) - 1) / shape;
  }

  virtual void set_lambda(double lambda) {
    if (lambda <= 0.0) scale = 0.0;
    else scale = (1 - shape) / lambda - (1 - shape) * loc;
  }

private:
  double loc /* mu */;
  double scale /* sigma */, shape /* k */;
};

class GEV : public Generator {
public:
  GEV(double _loc = 0.0, double _scale = 1.0, double _shape = 1.0) :
    e(1.0), loc(_loc), scale(_scale), shape(_shape) {
    assert(shape != 0.0);
    D("GEV(loc=%f, scale=%f, shape=%f)", loc, scale, shape);
  }

  virtual double generate(double U = -1.0) {
    return loc + scale * (pow(e.generate(U), -shape) - 1) / shape;
  }

private:
  Exponential e;
  double loc /* mu */, scale /* sigma */, shape /* k */;
};

class Discrete : public Generator {
public:
  ~Discrete() { delete def; }
  Discrete(Generator* _def = NULL) : def(_def) {
    if (def == NULL) def = new Fixed(0.0);
  }

  virtual double generate(double U = -1.0) {
    double Uc = U;
    if (pv.size() > 0 && U < 0.0) U = drand48();

    double sum = 0;
 
    for (auto p: pv) {
      sum += p.first;
      if (U < sum) return p.second;
    }

    return def->generate(Uc);
  }

  void add(double p, double v) {
    pv.push_back(std::pair<double,double>(p, v));
  }

private:
  Generator *def;
  std::vector< std::pair<double,double> > pv;
};

class Bimodal : public Generator {
public:
  Bimodal(double _ratio, double _v1, double _v2) :
    ratio(_ratio), v1(_v1), v2(_v2) { }

  virtual double generate(double U = -1.0) {
    if (U < 0.0) U = drand48();
    if (U > ratio)
      return v2;
    else
      return v1;
  }

private:
  double ratio, v1, v2;
};

class Zipf : public Generator {
public:
  Zipf(int _n, double _theta, long permutation_seed) : n(_n), theta(_theta) {
    this->alpha = 1.0 / (1.0 - this->theta);
    this->zetan = this->zeta(this->n, this->theta);
    this->eta = (1.0 - pow(2.0 / n, 1.0 - theta)) / (1.0 - this->zeta(2.0, this->theta) / this->zetan);
    this->permutation = new int[this->n];
    for (int i=0; i<n; i++)
      this->permutation[i] = i;
    if (permutation_seed) {
      drand48_data drand_buf;
      srand48_r(permutation_seed, &drand_buf);
      for (int i=n-1; i>0; i--) {
        long j;
        lrand48_r(&drand_buf, &j);
        j %= i + 1;
        int tmp = this->permutation[i];
        this->permutation[i] = this->permutation[j];
        this->permutation[j] = tmp;
      }
    }
  }

  ~Zipf() {
    delete[] this->permutation;
  }

  virtual double generate(double U = -1.0) {
    int idx;

    if (U < 0.0) U = drand48();
    double uz = U * this->zetan;
    if (uz < 1)
      idx = 0;
    if (uz < 1.0 + pow(0.5, this->theta))
      idx = 1;
    idx = (int)(1.0 * this->n * pow(this->eta*U - this->eta + 1.0, this->alpha));

    assert(idx >= 0 && idx < this->n);
    return permutation[idx];
  }

private:
  int n;
  double theta;
  double alpha;
  double zetan;
  double eta;
  int *permutation;

  double zeta(int n, double theta) {
    double ret = 0;
    for (int i=1; i<=n; i++)
      ret += pow(i, -theta);
    return ret;
  }
};

class FileGenerator : public Generator {
private:
  std::vector<double> samples;

public:
  FileGenerator(const char *filename) {
    double n;
    std::ifstream infile(filename);
    while (infile >> n)
      samples.push_back(n);
  }

  virtual double generate(double U = -1.0) {
    if (U < 0.0) U = drand48();
    return samples[U * samples.size()];
  }
};

class LogNormal : public Generator {
public:
  LogNormal(double _mu, double _sigma) :
    mu(_mu), sigma(_sigma), normal(0, 1) {
    }

  virtual double generate(double U = -1.0) {
    double y = normal.generate(U);
    return exp(mu+y*sigma);
  }

private:
  double mu, sigma;
  Normal normal;
};

class KeyGenerator {
public:
  KeyGenerator(Generator* _g, double _max = 10000) : g(_g), max(_max) {}
  std::string generate(uint64_t ind) {
    uint64_t h = fnv_64(ind);
    double U = (double) h / ULLONG_MAX;
    double G = g->generate(U);
    int keylen = MAX(round(G), floor(log10(max)) + 1);
    char key[256];
    snprintf(key, 256, "%0*" PRIu64, keylen, ind);

    //    D("%d = %s", ind, key);
    return std::string(key);
  }
private:
  Generator* g;
  double max;
};

class CustomKeyGenerator {
public:
  CustomKeyGenerator(Generator* t, Generator* r) : comp_time(t), val_size(r) {}
  std::string generate() {
    int time = (int) comp_time->generate();
    int  value = (int) val_size->generate();
    char key[256];
    snprintf(key, 256, "%d:%d", time, value);

    return std::string(key);
  }
private:
    Generator* comp_time;
    Generator* val_size;
};
Generator* createFacebookKey();
Generator* createFacebookValue();
Generator* createFacebookIA();
Generator* createGenerator(std::string str);
Generator *createPopularityGenerator(std::string str, long records, long permutation_seed);
void deleteGenerator(Generator* gen);

#endif // GENERATOR_H
