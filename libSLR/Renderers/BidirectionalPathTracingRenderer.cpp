//
//  BidirectionalPathTracingRenderer.cpp
//  SLR
//
//  Created by 渡部 心 on 2016/01/31.
//  Copyright © 2016年 渡部 心. All rights reserved.
//

#include "BidirectionalPathTracingRenderer.h"

#include "../Core/RenderSettings.h"
#include "../Helper/ThreadPool.h"
#include "../Core/XORShift.h"
#include "../Core/ImageSensor.h"
#include "../Core/RandomNumberGenerator.h"
#include "../Memory/ArenaAllocator.h"
#include "../Core/cameras.h"
#include "../Core/SurfaceObject.h"

namespace SLR {
    BidirectionalPathTracingRenderer::BidirectionalPathTracingRenderer(uint32_t spp) : m_samplesPerPixel(spp) {
    }
    
    void BidirectionalPathTracingRenderer::render(const Scene &scene, const RenderSettings &settings) const {
#ifdef DEBUG
        uint32_t numThreads = 1;
#else
        uint32_t numThreads = std::thread::hardware_concurrency();
#endif
        XORShift topRand(settings.getInt(RenderSettingItem::RNGSeed));
        std::unique_ptr<ArenaAllocator[]> mems = std::unique_ptr<ArenaAllocator[]>(new ArenaAllocator[numThreads]);
        std::unique_ptr<XORShift[]> rngs = std::unique_ptr<XORShift[]>(new XORShift[numThreads]);
        for (int i = 0; i < numThreads; ++i) {
            new (mems.get() + i) ArenaAllocator();
            new (rngs.get() + i) XORShift(topRand.getUInt());
        }
        std::unique_ptr<RandomNumberGenerator*[]> rngRefs = std::unique_ptr<RandomNumberGenerator*[]>(new RandomNumberGenerator*[numThreads]);
        for (int i = 0; i < numThreads; ++i)
            rngRefs[i] = &rngs[i];
        
        const Camera* camera = scene.getCamera();
        ImageSensor* sensor = camera->getSensor();
        
        Job job;
        job.scene = &scene;
        
        job.mems = mems.get();
        job.rngs = rngRefs.get();
        
        job.camera = camera;
        job.timeStart = settings.getFloat(RenderSettingItem::TimeStart);
        job.timeEnd = settings.getFloat(RenderSettingItem::TimeEnd);
        
        job.sensor = sensor;
        job.imageWidth = settings.getInt(RenderSettingItem::ImageWidth);
        job.imageHeight = settings.getInt(RenderSettingItem::ImageHeight);
        job.numPixelX = sensor->tileWidth();
        job.numPixelY = sensor->tileHeight();
        
        uint32_t exportPass = 1;
        uint32_t imgIdx = 0;
        uint32_t endIdx = 16;
        
        sensor->init(job.imageWidth, job.imageHeight);
        sensor->addSeparatedBuffers(numThreads);
        
        for (int s = 0; s < m_samplesPerPixel; ++s) {
            ThreadPool threadPool(numThreads);
            for (int ty = 0; ty < sensor->numTileY(); ++ty) {
                for (int tx = 0; tx < sensor->numTileX(); ++tx) {
                    job.basePixelX = tx * sensor->tileWidth();
                    job.basePixelY = ty * sensor->tileHeight();
                    threadPool.enqueue(std::bind(&Job::kernel, job, std::placeholders::_1));
                }
            }
            threadPool.wait();
            
            if ((s + 1) == exportPass) {
                char filename[256];
                sprintf(filename, "%03u.bmp", imgIdx);
                sensor->saveImage(filename, settings.getFloat(RenderSettingItem::Brightness) / (s + 1));
                printf("%u samples: %s\n", exportPass, filename);
                ++imgIdx;
                if (imgIdx == endIdx)
                    break;
                exportPass += exportPass;
            }
        }
        
        //    sensor.saveImage("output.png", settings.getFloat(RenderSettingItem::SensorResponse) / numSamples);
    }
    
    void BidirectionalPathTracingRenderer::Job::kernel(uint32_t threadID) {
        ArenaAllocator &mem = mems[threadID];
        RandomNumberGenerator &rng = *rngs[threadID];
        for (int ly = 0; ly < numPixelY; ++ly) {
            for (int lx = 0; lx < numPixelX; ++lx) {
                float time = timeStart + rng.getFloat0cTo1o() * (timeEnd - timeStart);
                float px = basePixelX + lx + rng.getFloat0cTo1o();
                float py = basePixelY + ly + rng.getFloat0cTo1o();
                
                float selectWLPDF;
                WavelengthSamples wls = WavelengthSamples::createWithEqualOffsets(rng.getFloat0cTo1o(), rng.getFloat0cTo1o(), &selectWLPDF);
                
                // initialize working area for the current pixel.
                curPx = px;
                curPy = py;
                wlHint = wls.selectedLambda;
                eyeVertices.clear();
                lightVertices.clear();
                
                // light subpath generation
                {
                    // select one light from all the lights in the scene.
                    float lightProb;
                    Light light;
                    scene->selectLight(rng.getFloat0cTo1o(), &light, &lightProb);
                    SLRAssert(!std::isnan(lightProb) && !std::isinf(lightProb), "lightProb: unexpected value detected: %f", lightProb);
                    
                    // sample a position (emittance) on the selected light's surface.
                    LightPosQuery lightQuery(time, wls);
                    LightPosSample lightSample(rng.getFloat0cTo1o(), rng.getFloat0cTo1o());
                    LightPosQueryResult lightResult;
                    SampledSpectrum Le0 = light.sample(lightQuery, lightSample, &lightResult);
                    EDF* edf = lightResult.surfPt.createEDF(wls, mem);
                    float lightAreaPDF = lightProb * lightResult.areaPDF;
                    SLRAssert(!std::isnan(lightResult.areaPDF)/* && !std::isinf(lightResult)*/, "areaPDF: unexpected value detected: %f", lightResult.areaPDF);
                    
                    // register the first light vertex.
                    lightVertices.emplace_back(lightResult.surfPt, Vector3D::Zero, Normal3D(0, 0, 1), mem.create<EDFProxy>(edf),
                                               Le0 / lightAreaPDF, lightAreaPDF, 1.0f, lightResult.posType, WavelengthSamples::Flag(0));
                    
                    // sample a direction from EDF, then create subsequent light subpath vertices by tracing in the scene.
                    EDFQuery edfQuery;
                    EDFSample LeSample(rng.getFloat0cTo1o(), rng.getFloat0cTo1o(), rng.getFloat0cTo1o());
                    EDFQueryResult LeResult;
                    SampledSpectrum Le1 = edf->sample(edfQuery, LeSample, &LeResult);
                    Ray ray(lightResult.surfPt.p + Ray::Epsilon * lightResult.surfPt.gNormal, lightResult.surfPt.shadingFrame.fromLocal(LeResult.dir_sn), time);
                    SampledSpectrum alpha = lightVertices.back().alpha * Le1 * (LeResult.dir_sn.z / LeResult.dirPDF);
                    generateSubPath(wls, alpha, ray, LeResult.dirPDF, LeResult.dirType, LeResult.dir_sn.z, true, rng, mem);
                }
                
                // eye subpath generation
                {
                    // sample a position (We0, spatial importance) on the lens surface of the camera.
                    LensPosQuery lensQuery(time, wls);
                    LensPosSample lensSample(rng.getFloat0cTo1o(), rng.getFloat0cTo1o());
                    LensPosQueryResult lensResult;
                    SampledSpectrum We0 = camera->sample(lensQuery, lensSample, &lensResult);
                    IDF* idf = camera->createIDF(lensResult.surfPt, wls, mem);
                    
                    // register the first eye vertex.
                    eyeVertices.emplace_back(lensResult.surfPt, Vector3D::Zero, Normal3D(0, 0, 1), mem.create<IDFProxy>(idf),
                                             We0 / (lensResult.areaPDF * selectWLPDF), lensResult.areaPDF, 1.0f, lensResult.posType, WavelengthSamples::Flag(0));
                    
                    // sample a direction (directional importance) from IDF, then create subsequent eye subpath vertices by tracing in the scene.
                    IDFSample WeSample(px / imageWidth, py / imageHeight);
                    IDFQueryResult WeResult;
                    SampledSpectrum We1 = idf->sample(WeSample, &WeResult);
                    Ray ray(lensResult.surfPt.p, lensResult.surfPt.shadingFrame.fromLocal(WeResult.dirLocal), time);
                    SampledSpectrum alpha = eyeVertices.back().alpha * We1 * (WeResult.dirLocal.z / WeResult.dirPDF);
                    generateSubPath(wls, alpha, ray, WeResult.dirPDF, WeResult.dirType, WeResult.dirLocal.z, false, rng, mem);
                }
                
                // connection
                for (int t = 1; t <= eyeVertices.size(); ++t) {
                    const BPTVertex &eVtx = eyeVertices[t - 1];
                    for (int s = 1; s <= lightVertices.size(); ++s) {
                        const BPTVertex &lVtx = lightVertices[s - 1];
                        
                        if (!scene->testVisibility(eVtx.surfPt, lVtx.surfPt, time))
                            continue;
                        
                        Vector3D connectionVector = eVtx.surfPt.p - lVtx.surfPt.p;
                        float dist2 = connectionVector.sqLength();
                        connectionVector /= std::sqrt(dist2);
                        
                        Vector3D cVecL = lVtx.surfPt.shadingFrame.toLocal(connectionVector);
                        DDFQuery queryLightEnd{lVtx.dirIn_sn, lVtx.gNormal_sn, wlHint, true};
                        SampledSpectrum revDDFL;
                        SampledSpectrum ddfL = lVtx.ddf->evaluate(queryLightEnd, cVecL, &revDDFL);
                        float eExtend2ndDirPDF;
                        float lExtend1stDirPDF = lVtx.ddf->evaluatePDF(queryLightEnd, cVecL, &eExtend2ndDirPDF);
                        float cosLightEnd = absDot(connectionVector, lVtx.surfPt.gNormal);
                        
                        Vector3D cVecE = eVtx.surfPt.shadingFrame.toLocal(-connectionVector);
                        DDFQuery queryEyeEnd{eVtx.dirIn_sn, eVtx.gNormal_sn, wlHint, false};
                        SampledSpectrum revDDFE;
                        SampledSpectrum ddfE = eVtx.ddf->evaluate(queryEyeEnd, cVecE, &revDDFE);
                        float lExtend2ndDirPDF;
                        float eExtend1stDirPDF = eVtx.ddf->evaluatePDF(queryEyeEnd, cVecE, &lExtend2ndDirPDF);
                        float cosEyeEnd = absDot(connectionVector, eVtx.surfPt.gNormal);
                        
                        float G = cosEyeEnd * cosLightEnd / dist2;
                        float wlProb = 1.0f;
                        if ((lVtx.wlFlags | eVtx.wlFlags) & WavelengthSamples::LambdaIsSelected)
                            wlProb = 1.0f / WavelengthSamples::NumComponents;
                        SampledSpectrum connectionTerm = ddfL * (G / wlProb) * ddfE;
                        if (connectionTerm == SampledSpectrum::Zero)
                            continue;
                        
                        // calculate 1st and 2nd subpath extending PDFs and probabilities.
                        // They can't be stored in advance because they depend on the connection.
                        float lExtend1stAreaPDF, lExtend1stRRProb, lExtend2ndAreaPDF, lExtend2ndRRProb;
                        {
                            lExtend1stAreaPDF = lExtend1stDirPDF * cosEyeEnd / dist2;
                            lExtend1stRRProb = s > 1 ? std::min((ddfL * cosLightEnd / lExtend1stDirPDF)[wlHint], 1.0f) : 1.0f;
                            
                            if (t > 1) {
                                BPTVertex &eVtxNextToEnd = eyeVertices[t - 2];
                                Vector3D dir2nd = eVtx.surfPt.p - eVtxNextToEnd.surfPt.p;
                                float dist2 = dir2nd.sqLength();
                                dir2nd /= std::sqrt(dist2);
                                lExtend2ndAreaPDF = lExtend2ndDirPDF * absDot(eVtxNextToEnd.surfPt.gNormal, dir2nd) / dist2;
                                lExtend2ndRRProb = std::min((revDDFE * absDot(eVtx.gNormal_sn, eVtx.dirIn_sn) / lExtend2ndDirPDF)[wlHint], 1.0f);
                            }
                        }
                        float eExtend1stAreaPDF, eExtend1stRRProb, eExtend2ndAreaPDF, eExtend2ndRRProb;
                        {
                            eExtend1stAreaPDF = eExtend1stDirPDF * cosLightEnd / dist2;
                            eExtend1stRRProb = t > 1 ? std::min((ddfE * cosEyeEnd / eExtend1stDirPDF)[wlHint], 1.0f) : 1.0f;
                            
                            if (s > 1) {
                                BPTVertex &lVtxNextToEnd = lightVertices[s - 2];
                                Vector3D dir2nd = lVtx.surfPt.p - lVtxNextToEnd.surfPt.p;
                                float dist2 = dir2nd.sqLength();
                                dir2nd /= std::sqrt(dist2);
                                eExtend2ndAreaPDF = eExtend2ndDirPDF * absDot(lVtxNextToEnd.surfPt.gNormal, dir2nd) / dist2;
                                eExtend2ndRRProb = std::min((revDDFL * absDot(lVtx.gNormal_sn, lVtx.dirIn_sn) / eExtend2ndDirPDF)[wlHint], 1.0f);
                            }
                        }
                        
                        // calculate MIS weight and store weighted contribution to a sensor.
                        float MISWeight = calculateMISWeight(lExtend1stAreaPDF, lExtend1stRRProb, lExtend2ndAreaPDF, lExtend2ndRRProb,
                                                             eExtend1stAreaPDF, eExtend1stRRProb, eExtend2ndAreaPDF, eExtend2ndRRProb, s, t);
//                        SLRAssert(!std::isinf(MISWeight) && !std::isnan(MISWeight), "invalid MIS weight.");
                        if (std::isinf(MISWeight) || std::isnan(MISWeight))
                            continue;
                        SampledSpectrum contribution = MISWeight * lVtx.alpha * connectionTerm * eVtx.alpha;
                        if (t > 1) {
                            sensor->add(px, py, wls, contribution);
                        }
                        else {
                            const IDF* idf = (const IDF*)eVtx.ddf->getDDF();
                            float hitPx, hitPy;
                            idf->calculatePixel(cVecE, &hitPx, &hitPy);
                            sensor->add(threadID, hitPx, hitPy, wls, contribution);
                        }
                    }
                }
                
                mem.reset();
            }
        }
    }
    
    void BidirectionalPathTracingRenderer::Job::generateSubPath(const WavelengthSamples &initWLs, const SampledSpectrum &initAlpha, const SLR::Ray &initRay, float dirPDF, DirectionType sampledType,
                                                                float cosLast, bool adjoint, RandomNumberGenerator &rng, SLR::ArenaAllocator &mem) {
        std::vector<BPTVertex> &vertices = adjoint ? lightVertices : eyeVertices;
        
        WavelengthSamples wls = initWLs;
        Ray ray = initRay;
        SampledSpectrum alpha = initAlpha;
        
        Intersection isect;
        SurfacePoint surfPt;
        float RRProb = 1.0f;
        while (scene->intersect(ray, &isect)) {
            isect.getSurfacePoint(&surfPt);
            Vector3D dirOut_sn = surfPt.shadingFrame.toLocal(-ray.dir);
            Normal3D gNorm_sn = surfPt.shadingFrame.toLocal(surfPt.gNormal);
            BSDF* bsdf = surfPt.createBSDF(wls, mem);
            
            float areaPDF = dirPDF * absDot(dirOut_sn, gNorm_sn) / (isect.dist * isect.dist);
            vertices.emplace_back(surfPt, dirOut_sn, gNorm_sn, mem.create<BSDFProxy>(bsdf), alpha, areaPDF, RRProb, sampledType, wls.flags);
            
            // implicit path (zero light subpath vertices, s = 0)
            if (!adjoint && surfPt.isEmitting()) {
                EDF* edf = surfPt.createEDF(wls, mem);
                SampledSpectrum Le0 = surfPt.emittance(wls);
                SampledSpectrum Le1 = edf->evaluate(EDFQuery(), dirOut_sn);
                
                float lightProb = scene->evaluateProb(Light(isect.obj));
                float extend1stAreaPDF = lightProb * surfPt.evaluateAreaPDF();
                float extend2ndAreaPDF = edf->evaluatePDF(EDFQuery(), dirOut_sn) * cosLast / (isect.dist * isect.dist);
                
                float MISWeight = calculateMISWeight(extend1stAreaPDF, 1.0f, extend2ndAreaPDF, 1.0f,
                                                     0.0f, 0.0f, 0.0f, 0.0f,
                                                     0, (uint32_t)vertices.size());
                SampledSpectrum contribution = MISWeight * alpha * Le0 * Le1;
                if (wls.flags & WavelengthSamples::LambdaIsSelected)
                    contribution *= WavelengthSamples::NumComponents;
                if (!std::isinf(MISWeight) && !std::isnan(MISWeight))
                    sensor->add(curPx, curPy, wls, contribution);
            }
            
            if (surfPt.atInfinity) {
                vertices.pop_back();
                break;
            }
            
            BSDFQuery fsQuery(dirOut_sn, gNorm_sn, wls.selectedLambda, DirectionType::All, adjoint);
            BSDFSample fsSample(rng.getFloat0cTo1o(), rng.getFloat0cTo1o(), rng.getFloat0cTo1o());
            BSDFQueryResult fsResult;
            BSDFReverseInfo revInfo;
            fsResult.reverse = &revInfo;
            SampledSpectrum fs = bsdf->sample(fsQuery, fsSample, &fsResult);
            if (fs == SampledSpectrum::Zero || fsResult.dirPDF == 0.0f)
                break;
            if (fsResult.dirType.isDispersive())
                wls.flags |= WavelengthSamples::LambdaIsSelected;
            float cosIn = absDot(fsResult.dir_sn, gNorm_sn);
            SampledSpectrum weight = fs * (cosIn / fsResult.dirPDF);
            
            // Russian roulette
            RRProb = std::min(weight[wlHint], 1.0f);
            if (rng.getFloat0cTo1o() < RRProb)
                weight /= RRProb;
            else
                break;
            
            alpha *= weight;
            SLRAssert(!weight.hasInf() && !weight.hasNaN(),
                      "weight: unexpected value detected:\nweight: %s\nfs: %s\nlength: %u, cos: %g, dirPDF: %g",
                      weight.toString().c_str(), fs.toString().c_str(), uint32_t(vertices.size()) - 1, cosIn, fsResult.dirPDF);
            
            Vector3D dirIn = surfPt.shadingFrame.fromLocal(fsResult.dir_sn);
            ray = Ray(surfPt.p + Ray::Epsilon * dirIn, dirIn, ray.time);
            
            BPTVertex &vtxNextToLast = vertices[vertices.size() - 2];
            vtxNextToLast.revAreaPDF = revInfo.dirPDF * cosLast / (isect.dist * isect.dist);
            vtxNextToLast.revRRProb = std::min((revInfo.fs * absDot(dirOut_sn, gNorm_sn) / revInfo.dirPDF)[wlHint], 1.0f);
//            SLRAssert(!std::isnan(vtxNextToLast.revAreaPDF) && !std::isinf(vtxNextToLast.revAreaPDF),
//                      "invalid reverse area PDF: %g, dirPDF: %g, cos: %g, dist2: %g", vtxNextToLast.revAreaPDF, revInfo.dirPDF, cosLast, isect.dist * isect.dist);
//            SLRAssert(!std::isnan(vtxNextToLast.revRRProb) && !std::isinf(vtxNextToLast.revRRProb),
//                      "invalid reverse RR probability: %g\nfs: %s\n, absDot: %g, dirPDF: %g",
//                      vtxNextToLast.revRRProb, revInfo.fs.toString().c_str(), absDot(dirOut_sn, gNorm_sn), revInfo.dirPDF);
            
            cosLast = cosIn;
            dirPDF = fsResult.dirPDF;
            sampledType = fsResult.dirType;
            isect = Intersection();
        }
    }
    
    // calculate power heuristic MIS weight
    float BidirectionalPathTracingRenderer::Job::calculateMISWeight(float lExtend1stAreaPDF, float lExtend1stRRProb, float lExtend2ndAreaPDF, float lExtend2ndRRProb,
                                                                    float eExtend1stAreaPDF, float eExtend1stRRProb, float eExtend2ndAreaPDF, float eExtend2ndRRProb,
                                                                    uint32_t numLVtx, uint32_t numEVtx) const {
        const uint32_t minEyeVertices = 1;
        const uint32_t minLightVertices = 0;
        FloatSum recMISWeight = 1;
        
        // extend/shorten light/eye subpath, not consider implicit light subpath reaching a lens.
        if (numEVtx > minEyeVertices) {
            const BPTVertex &eyeEndVtx = eyeVertices[numEVtx - 1];
            float PDFRatio = lExtend1stAreaPDF * lExtend1stRRProb / (eyeEndVtx.areaPDF * eyeEndVtx.RRProb);
            bool shortenIsDeltaSampled = eyeEndVtx.sampledType.isDelta();
            if (!shortenIsDeltaSampled)
                recMISWeight += PDFRatio * PDFRatio;
            bool prevIsDeltaSampled = shortenIsDeltaSampled;
            if (numEVtx - 1 > minEyeVertices) {
                const BPTVertex &newLightVtx = eyeVertices[numEVtx - 2];
                PDFRatio *= lExtend2ndAreaPDF * lExtend2ndRRProb / (newLightVtx.areaPDF * newLightVtx.RRProb);
                shortenIsDeltaSampled = newLightVtx.sampledType.isDelta();
                if (!shortenIsDeltaSampled && !prevIsDeltaSampled)
                    recMISWeight += PDFRatio * PDFRatio;
                prevIsDeltaSampled = shortenIsDeltaSampled;
                for (int t = numEVtx - 2; t > minEyeVertices; --t) {
                    const BPTVertex &newLightVtx = eyeVertices[t - 1];
                    PDFRatio *= newLightVtx.revAreaPDF * newLightVtx.revRRProb / (newLightVtx.areaPDF * newLightVtx.RRProb);
                    shortenIsDeltaSampled = newLightVtx.sampledType.isDelta();
                    if (!shortenIsDeltaSampled && !prevIsDeltaSampled)
                        recMISWeight += PDFRatio * PDFRatio;
                    prevIsDeltaSampled = shortenIsDeltaSampled;
                }
            }
        }
        
        // extend/shorten eye/light subpath, consider implicit eye subpath reaching a light.
        if (numLVtx > minLightVertices) {
            const BPTVertex &lightEndVtx = lightVertices[numLVtx - 1];
            float PDFRatio = eExtend1stAreaPDF * eExtend1stRRProb / (lightEndVtx.areaPDF * lightEndVtx.RRProb);
            bool shortenIsDeltaSampled = lightEndVtx.sampledType.isDelta();
            if (!shortenIsDeltaSampled)
                recMISWeight += PDFRatio * PDFRatio;
            bool prevIsDeltaSampled = shortenIsDeltaSampled;
            if (numLVtx - 1 > minLightVertices) {
                const BPTVertex &newEyeVtx = lightVertices[numLVtx - 2];
                PDFRatio *= eExtend2ndAreaPDF * eExtend2ndRRProb / (newEyeVtx.areaPDF * newEyeVtx.RRProb);
                shortenIsDeltaSampled = newEyeVtx.sampledType.isDelta();
                if (!shortenIsDeltaSampled && !prevIsDeltaSampled)
                    recMISWeight += PDFRatio * PDFRatio;
                prevIsDeltaSampled = shortenIsDeltaSampled;
                for (int s = numLVtx - 2; s > minLightVertices; --s) {
                    const BPTVertex &newEyeVtx = lightVertices[s - 1];
                    PDFRatio *= newEyeVtx.revAreaPDF * newEyeVtx.revRRProb / (newEyeVtx.areaPDF * newEyeVtx.RRProb);
                    shortenIsDeltaSampled = newEyeVtx.sampledType.isDelta();
                    if (!shortenIsDeltaSampled && !prevIsDeltaSampled)
                        recMISWeight += PDFRatio * PDFRatio;
                    prevIsDeltaSampled = shortenIsDeltaSampled;
                }
            }
        }
        
        return 1.0f / recMISWeight;
    }
}
