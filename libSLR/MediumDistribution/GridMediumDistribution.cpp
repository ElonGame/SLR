//
//  GridMediumDistribution.cpp
//
//  Created by 渡部 心 on 2017/01/04.
//  Copyright (c) 2017年 渡部 心. All rights reserved.
//

#include "GridMediumDistribution.h"

#include "../Core/light_path_sampler.h"

namespace SLR {
    bool GridMediumDistribution::subdivide(Allocator* mem, MediumDistribution** fragments, uint32_t* numFragments) const {
        SLRAssert_NotImplemented();
        return true;
    }
    
    bool GridMediumDistribution::interact(const Ray &ray, float distanceLimit, const WavelengthSamples &wls, LightPathSampler &pathSampler,
                                          MediumInteraction *mi, SampledSpectrum *medThroughput, bool* singleWavelength) const {
        SLRAssert_NotImplemented();
        return false;
    }
    
    SampledSpectrum GridMediumDistribution::evaluateTransmittance(Ray &ray, float distanceLimit, const WavelengthSamples &wls, SLR::LightPathSampler &pathSampler, bool *singleWavelength) const {
        SLRAssert_NotImplemented();
        
        return SampledSpectrum::Zero;
    }
    
    void GridMediumDistribution::getMediumPoint(const MediumInteraction &mi, MediumPoint* medPt) const {
        SLRAssert_NotImplemented();
    }
    
    SampledSpectrum GridMediumDistribution::getExtinctionCoefficient(const Point3D &param, const WavelengthSamples &wls) const {
        SLRAssert_NotImplemented();
        
        return SampledSpectrum::Zero;
    }
    
    SampledSpectrum GridMediumDistribution::getAlbedo(const Point3D &param, const WavelengthSamples &wls) const {
        SLRAssert_NotImplemented();
        
        return SampledSpectrum::Zero;
    }
    
    void GridMediumDistribution::sample(float u0, float u1, float u2, MediumPoint *medPt, float *volumePDF) const {
        SLRAssert_NotImplemented();
    }
    
    float GridMediumDistribution::evaluateVolumePDF(const MediumPoint &medPt) const {
        return 1.0f / volume();
    }
    
    
    
    bool SubGridMediumDistribution::interact(const Ray &ray, float distanceLimit, const WavelengthSamples &wls, LightPathSampler &pathSampler,
                                 MediumInteraction *mi, SampledSpectrum *medThroughput, bool* singleWavelength) const {
        SLRAssert_NotImplemented();
        return false;
    }
    
    SampledSpectrum SubGridMediumDistribution::evaluateTransmittance(Ray &ray, float distanceLimit, const WavelengthSamples &wls, SLR::LightPathSampler &pathSampler, bool *singleWavelength) const {
        SLRAssert_NotImplemented();
        return SampledSpectrum::Zero;
    }
    
    void SubGridMediumDistribution::getMediumPoint(const MediumInteraction &mi, MediumPoint* medPt) const {
        SLRAssert_NotImplemented();
    }
    
    SampledSpectrum SubGridMediumDistribution::getExtinctionCoefficient(const Point3D &param, const WavelengthSamples &wls) const {
        SLRAssert_NotImplemented();
        
        return SampledSpectrum::Zero;
    }
    
    SampledSpectrum SubGridMediumDistribution::getAlbedo(const Point3D &param, const WavelengthSamples &wls) const {
        SLRAssert_NotImplemented();
        
        return SampledSpectrum::Zero;
    }
    
    void SubGridMediumDistribution::sample(float u0, float u1, float u2, MediumPoint *medPt, float *volumePDF) const {
        SLRAssert_NotImplemented();
    }
    
    float SubGridMediumDistribution::evaluateVolumePDF(const MediumPoint &medPt) const {
        return 1.0f / volume();
    }
}
