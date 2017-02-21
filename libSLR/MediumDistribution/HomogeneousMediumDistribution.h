//
//  HomogeneousMediumDistribution.h
//
//  Created by 渡部 心 on 2016/12/31.
//  Copyright (c) 2016年 渡部 心. All rights reserved.
//

#ifndef __SLR_HomogeneousMediumDistribution__
#define __SLR_HomogeneousMediumDistribution__

#include "../defines.h"
#include "../declarations.h"
#include "../Core/geometry.h"

namespace SLR {
    class HomogeneousMediumDistribution : public MediumDistribution {
        BoundingBox3D m_region;
        const AssetSpectrum* m_sigma_s;
        const AssetSpectrum* m_sigma_e;
        
    public:
        HomogeneousMediumDistribution(const BoundingBox3D &region, const AssetSpectrum* sigma_s, const AssetSpectrum* sigma_e) :
        MediumDistribution(sigma_e->calcBounds()), m_region(region), m_sigma_s(sigma_s), m_sigma_e(sigma_e) { }
        
        bool subdivide(Allocator* mem, MediumDistribution** fragments, uint32_t* numFragments) const override { return false; }
        
        BoundingBox3D bounds() const override { return m_region; }
        bool contains(const Point3D &p) const override { return m_region.contains(p); }
        bool intersectBoundary(const Ray &ray, float* distToBoundary, bool* enter) const override {
            return m_region.intersectBoundary(ray, distToBoundary, enter);
        }
        bool interact(const Ray &ray, float distanceLimit, const WavelengthSamples &wls, LightPathSampler &pathSampler,
                      MediumInteraction* mi, SampledSpectrum* medThroughput, bool* singleWavelength) const override;
        SampledSpectrum evaluateTransmittance(Ray &ray, float distanceLimit, const WavelengthSamples &wls, LightPathSampler &pathSampler,
                                              bool* singleWavelength) const override;
        void getMediumPoint(const MediumInteraction &mi, MediumPoint* medPt) const override;
        SampledSpectrum getExtinctionCoefficient(const Point3D &param, const WavelengthSamples &wls) const override;
        SampledSpectrum getAlbedo(const Point3D &param, const WavelengthSamples &wls) const override;
        float volume() const override { return m_region.volume(); }
        void sample(float u0, float u1, float u2, MediumPoint* medPt, float* volumePDF) const override;
        float evaluateVolumePDF(const MediumPoint& medPt) const override;
    };
}

#endif /* __SLR_HomogeneousMediumDistribution__ */
