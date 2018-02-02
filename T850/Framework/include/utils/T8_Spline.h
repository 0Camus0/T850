#pragma once
#include <vector>
#include "xMaths.h"
namespace t800 {
  struct SplinePoint : public XVECTOR3 {
  public:
    SplinePoint() : m_length(0), m_velocity(0), m_LookAtCenter(true) {}
    SplinePoint(float a , float b , float c) : m_length(0), m_velocity(0), m_LookAtCenter(true){
      x = a;
      y = b;
      z = c;
    }
    float m_length;
    float m_velocity;
    XVECTOR3 m_rotation;
    bool m_LookAtCenter;
  };
  class Spline {
  public:
    const float STEP_SIZE = 0.005f;
    void Init();
    void ReCalculateSegmentsLength();
    explicit Spline(bool _looped = false) : m_looped(_looped), m_totalLength(0) {}
    explicit Spline(std::vector<SplinePoint>& points, bool _looped = false) : 
      m_looped(_looped), m_points(points), m_totalLength(0) {}
    SplinePoint GetPoint(float t);
    XVECTOR3 GetGradient(float t);
    float GetSegmentLength(int node);
    float GetNormalizedOffset(float p);

    std::vector<SplinePoint> m_points;
    float m_totalLength;
    bool m_looped;
  };

  class SplineAgent {
  private:
    float realLoc;
  public:
    SplineAgent() {
      realLoc = 0;
      m_acceleration = 0;
      m_velocity = 0;
      m_moving = false;
      m_pSpline = 0;
    }
    void Update(float delta);
    void SetOffset(float);
    Spline* m_pSpline;
    SplinePoint m_actualPoint;
    XVECTOR3 m_actualGradient;

    float m_acceleration;
    bool m_moving;
    float m_velocity;
  };
}