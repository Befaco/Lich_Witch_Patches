#ifndef __DcFilter_h__
#define __DcFilter_h__

#include "FloatArray.h"

class DcFilter {
private:
  const float lambda;
  float x1, y1;
public:
  // differentiator with leaky integrator 
  DcFilter(float lambda = 0.995): lambda(lambda), x1(0), y1(0) {}

  /* process a single sample and return the result */
  float process(float x){
    y1 = x - x1 + lambda*y1;
    x1 = x;
    return y1;
  }
  
  void process(float* input, float* output, size_t size){
    float x;
    float y = y1;
    while(size--){
      x = *input++;
      y = x - x1 + lambda*y;
      x1 = x;
      *output++ = y;
    }
    y1 = y;
  }
  
  /* perform in-place processing */
  void process(float* buf, int size){
    process(buf, buf, size);
  }

  void process(FloatArray in){
    process(in, in, in.getSize());
  }

  void process(FloatArray in, FloatArray out){
    ASSERT(out.getSize() >= in.getSize(), "output array must be at least as long as input");
    process(in, out, in.getSize());
  }
};

class StereoDcFilter {
private:
  DcFilter left, right;
public:
  StereoDcFilter(float lambda = 0.995): left(lambda), right(lambda) {}
  void process(AudioBuffer &buffer){
    left.process(buffer.getSamples(LEFT_CHANNEL));
    right.process(buffer.getSamples(RIGHT_CHANNEL));
  }
};

#endif // __DcFilter_h__
