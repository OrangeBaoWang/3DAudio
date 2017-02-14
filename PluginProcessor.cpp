/*
 PluginProcessor.cpp
 
 The meat of 3DAudio's audio processing and plugin related code.

 Copyright (C) 2017  Andrew Barker
 
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 
 The author can be contacted via email at andrew.barker.12345@gmail.com.
*/

#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "Data.h"
#include <fstream>

// the global hrir data that gets one instance across multiple plugin instances
float***** HRIRdata;
float****  HRIRdataPoles;

//==============================================================================
ThreeDAudioProcessor::ThreeDAudioProcessor()
{
    // if there are no instances going, load the global hrir data for all possible future instances
    if (numRefs == 0) {
        // unified poles, compact data
        // binary hrtf file name
        String path;
      #ifdef __APPLE__
        path = File::getSpecialLocation(File::currentApplicationFile).getFullPathName();
        path += "/Contents/3DAudioData.bin";
      #elif _WIN32
        path = File::getSpecialLocation(File::currentApplicationFile).getParentDirectory().getFullPathName();
	path += "/3DAudioData.bin";
      #endif
        // basic read (cross platform)
        // open the stream
        std::ifstream is (path.getCharPointer(), std::ios::binary);
        if (is.good()) {
            HRIRdata = new float****[numDistanceSteps];
            for (int d = 0; d < numDistanceSteps; ++d) {
                HRIRdata[d] = new float***[numAzimuthSteps/2+1];
                for (int a = 0; a < numAzimuthSteps/2+1; ++a) {
                    HRIRdata[d][a] = new float**[numElevationSteps-1];
                    for (int e = 0; e < numElevationSteps-1; ++e) {
                        HRIRdata[d][a][e] = new float*[2];
                        for (int ch = 0; ch < 2; ++ch) {
                            HRIRdata[d][a][e][ch] = new float[numTimeSteps];
							is.read((char*)HRIRdata[d][a][e][ch], numTimeSteps * sizeof(float));
                        }
                    }
                }
            }
            // pole data is the same for both channels
            HRIRdataPoles = new float***[numDistanceSteps];
			std::array<float, numTimeSteps> poleDataBuffer;
            for (int d = 0; d < numDistanceSteps; ++d) {
                HRIRdataPoles[d] = new float**[2];
                HRIRdataPoles[d][0] = new float*[2]; // ele = 0 pole
                HRIRdataPoles[d][1] = new float*[2]; // ele = 180 pole
                HRIRdataPoles[d][0][0] = new float[numTimeSteps];
                HRIRdataPoles[d][0][1] = new float[numTimeSteps];
				is.read((char*)&poleDataBuffer[0], numTimeSteps * sizeof(float));
                for (int t = 0; t < numTimeSteps; ++t)
					HRIRdataPoles[d][0][1][t] = HRIRdataPoles[d][0][0][t] = poleDataBuffer[t];
                HRIRdataPoles[d][1][0] = new float[numTimeSteps];
                HRIRdataPoles[d][1][1] = new float[numTimeSteps];
				is.read((char*)&poleDataBuffer[0], numTimeSteps * sizeof(float));
				for (int t = 0; t < numTimeSteps; ++t) 
					HRIRdataPoles[d][1][1][t] = HRIRdataPoles[d][1][0][t] = poleDataBuffer[t];
            }
			is.close();
        } else {
            // failed to open hrtf binary file, load up zeros
            HRIRdata = new float****[numDistanceSteps];
            for (int d = 0; d < numDistanceSteps; ++d) {
                HRIRdata[d] = new float***[numAzimuthSteps];
                for (int a = 0; a < numAzimuthSteps; ++a) {
                    HRIRdata[d][a] = new float**[numElevationSteps];
                    for (int e = 0; e < numElevationSteps; ++e) {
                        HRIRdata[d][a][e] = new float*[2];
                        for (int ch = 0; ch < 2; ++ch) {
                            HRIRdata[d][a][e][ch] = new float[numTimeSteps];
                            for (int t = 0; t < numTimeSteps; ++t) {
                                HRIRdata[d][a][e][ch][t] = 0;
                            }
                        }
                    }
                }
            }
            // pole data is the same for both channels
            HRIRdataPoles = new float***[numDistanceSteps];
            for (int d = 0; d < numDistanceSteps; ++d) {
                HRIRdataPoles[d] = new float**[2];
                HRIRdataPoles[d][0] = new float*[2]; // ele = 0 pole
                HRIRdataPoles[d][0][0] = new float[numTimeSteps];
                HRIRdataPoles[d][0][1] = new float[numTimeSteps];
                HRIRdataPoles[d][1] = new float*[2]; // ele = 180 pole
                HRIRdataPoles[d][1][0] = new float[numTimeSteps];
                HRIRdataPoles[d][1][1] = new float[numTimeSteps];
                for (int t = 0; t < numTimeSteps; ++t) {
                    HRIRdataPoles[d][0][0][t] = 0;
                    HRIRdataPoles[d][0][1][t] = 0;
                    HRIRdataPoles[d][1][0][t] = 0;
                    HRIRdataPoles[d][1][1][t] = 0;
                }
            }
        }
    }

    // increment plugin reference count
    ++numRefs;
    
    // pre-allocate space for maximum number of playableSources, so we don't have to in processBlock()
    playableSources.resize(maxNumSources);
    
    // load up one source as the default
    sources.load(std::vector<SoundSource>(1));
    
    // add plugin params for automation source position from DAW
    for (int i = 0; i < sourcePathPositionsFromDAW.size(); ++i)
        addParameter(sourcePathPositionsFromDAW[i] = new AudioParameterFloat ("Source " + String(i+1) + " Position",
                                                                              "Source " + String(i+1) + " Position",
                                                                              NormalisableRange<float>(0, 1), 0));
}

ThreeDAudioProcessor::~ThreeDAudioProcessor()
{
    // cleanup memeory for undo's
    clearUndoHistory();

    // cleanup hrir data if we are closing the only plugin instance
    if (numRefs == 1) {
        for (int d = numDistanceSteps-1; d >= 0; --d) {
            //for (int a = numAzimuthSteps-1; a >= 0; --a) {
            for (int a = numAzimuthSteps/2; a >= 0; --a) {
                //for (int e = numElevationSteps-1; e >= 0; --e) {
                for (int e = numElevationSteps-2; e >= 0; --e) {
                    for (int ch = 1; ch >= 0; --ch) {
                        delete[] HRIRdata[d][a][e][ch];
                    }
                    delete[] HRIRdata[d][a][e];
                }
                delete[] HRIRdata[d][a];
            }
            delete[] HRIRdata[d];
        }
        delete[] HRIRdata;
        for (int d = numDistanceSteps-1; d >= 0; --d) {
            for (int e = 1; e >= 0; --e) {
                for (int ch = 1; ch >= 0; --ch) {
                    delete[] HRIRdataPoles[d][e][ch];
                }
                delete[] HRIRdataPoles[d][e];
            }
            delete[] HRIRdataPoles[d];
        }
        delete[] HRIRdataPoles;
    }
    
    // decrement plugin reference count
    --numRefs;
}

// saves the current sources state beforeOrAfter == -1 -> before edit w/ reset,
//                                 beforeOrAfter == 0 -> before edit,
//                                 beforeOrAfter == 1 -> after edit
void ThreeDAudioProcessor::saveCurrentState(const int beforeOrAfter)
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        switch (beforeOrAfter) {
            case -1:
                // reset global before state
                beforeUndo.clear();
                // save the sources before edit state
                for (const auto& source : *copy) {
                    beforeUndo.emplace_back(source);
                    // set the paths to changed so that the sources' paths are updated correctly when performing undo/redos
                    beforeUndo.back().setPathChanged(true);
                    beforeUndo.back().setPathPosChanged(true);
                }
                break;
            case 0:
                // if before edit snapshot of sources is empty
                if (beforeUndo.size() == 0) {
                    // save the sources before edit state
                    for (const auto& source : *copy) {
                        beforeUndo.emplace_back(source);
                        // set the paths to changed so that the sources' paths are updated correctly when performing undo/redos
                        beforeUndo.back().setPathChanged(true);
                        beforeUndo.back().setPathPosChanged(true);
                    }
                }
                break;
            case 1:
                // if we have the source state before edit available
                if (beforeUndo.size() > 0) {
                    // new undo/redo transaction
                    beginNewTransaction();
                    // make sure current is cleared
                    currentUndo.clear();
                    // get the current sources states
                    for (const auto& source : *copy) {
                        currentUndo.emplace_back(source);
                        // set the paths to changed so that the sources' paths are updated correctly when performing undo/redos
                        currentUndo.back().setPathChanged(true);
                        currentUndo.back().setPathPosChanged(true);
                    }
                    // store the sources states of before and after
                    perform(new EditSources(beforeUndo, currentUndo, this));
                    // cleanup
                    beforeUndo.clear();
                    currentUndo.clear();
                }
                break;
        }
    }
//    Sources* copy = nullptr;
//    const Locker lock (sources.get(copy));
//    if (copy) {
//        Sources& before = beforeUndo.getResource();
//        const Locker lockBefore (beforeUndo.getLock());
//        switch (beforeOrAfter) {
//            case -1:
//                // reset global before state
//                before.clear();
//                // save the sources before edit state
//                for (const auto& source : *copy) {
//                    before.emplace_back(source);
//                    // set the paths to changed so that the sources' paths are updated correctly when performing undo/redos
//                    before.back().setPathChanged(true);
//                    before.back().setPathPosChanged(true);
//                }
//                break;
//            case 0:
//                // if before edit snapshot of sources is empty
//                if (before.size() == 0) {
//                    // save the sources before edit state
//                    for (const auto& source : *copy) {
//                        before.emplace_back(source);
//                        // set the paths to changed so that the sources' paths are updated correctly when performing undo/redos
//                        before.back().setPathChanged(true);
//                        before.back().setPathPosChanged(true);
//                    }
//                }
//                break;
//            case 1:
//                // if we have the source state before edit available
//                if (before.size() > 0) {
//                    Sources& current = currentUndo.getResource();
//                    const Locker lockCurrent (currentUndo.getLock());
//                    // new undo/redo transaction
//                    beginNewTransaction();
//                    // make sure current is cleared
//                    current.clear();
//                    // get the current sources states
//                    for (const auto& source : *copy) {
//                        current.emplace_back(source);
//                        // set the paths to changed so that the sources' paths are updated correctly when performing undo/redos
//                        current.back().setPathChanged(true);
//                        current.back().setPathPosChanged(true);
//                    }
//                    // store the sources states of before and after
//                    perform(new EditSources(beforeUndo, currentUndo, this));
//                    // cleanup
//                    before.clear();
//                    current.clear();
//                }
//                break;
//        }
//    }
}

bool ThreeDAudioProcessor::addSourceAtXYZ(const float (&xyz)[3])
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy && copy->size() < maxNumSources) {
        // get the sources before adding the new source
        saveCurrentState(-1);
        // add a new source at this xyz pos
        copy->emplace_back(std::array<float, 3>{xyz[0], xyz[1], xyz[2]});
        copy->back().setSourceSelected(true);
        // new undo/redo transaction
        saveCurrentState(1);
        // update all copies of the sources with the change
        sources.update(copy);
        return true;
    }
    return false;
}

void ThreeDAudioProcessor::copySelectedSources()
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        bool doUndoableAction = false;
        const int beforeNumSources = copy->size();
        for (int s = 0; s < beforeNumSources; ++s) {
            if ((*copy)[s].getSourceSelected()) {
                // are there any selected path points ?
                const std::vector<bool> pathPtsSelected = (*copy)[s].getSelectedPathPoints();
                if (std::find(pathPtsSelected.cbegin(), pathPtsSelected.cend(), true) != pathPtsSelected.cend()) {
                    // if so copy these inside of their respective source's path points
                    // get the sources before copying
                    if (!doUndoableAction) {
                        saveCurrentState(1);
                        saveCurrentState(0);
                        doUndoableAction = true;
                    }
                    (*copy)[s].copySelectedPathPoints();
                } else {
                    // if no path points are selected, then copy the whole source
                    // deselect the source we are copying
                    (*copy)[s].setSourceSelected(false);
                    if (copy->size() < maxNumSources) {
                        // get the sources before copying
                        if (!doUndoableAction) {
                            saveCurrentState(1);
                            saveCurrentState(0);
                            doUndoableAction = true;
                        }
                        // add a new source that is an exact copy of the sourceToCopy
                        copy->emplace_back(beforeUndo[s]);
//                        {
//                            const Locker lockBefore (beforeUndo.getLock());
//                            copy->emplace_back(beforeUndo.getResource()[s]);
//                        }
                        // select the new copy of the source and all its path control points
                        copy->back().setSourceSelected(true);
                        copy->back().setAllPathPointsSelected(true);
                    }
                }
            }
        }
        if (doUndoableAction) {
            sources.update(copy);
            for (auto& source : *copy) {
                source.doneUpdatingPath();
                source.doneUpdatingPathPos();
            }
            pathChanged = true;
            pathPosChanged = true;
        }
    }
}

void ThreeDAudioProcessor::getSourcePosXYZ(const int sourceIndex, float (&xyz)[3]) const
{
    const Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        const std::array<float,3> pos = (*copy)[sourceIndex].getPosXYZ();
        xyz[0] = pos[0];
        xyz[1] = pos[1];
        xyz[2] = pos[2];
    }
}

void ThreeDAudioProcessor::setSourceSelected(const int sourceIndex, const bool newSelectedState)
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        // set source's selected state
        (*copy)[sourceIndex].setSourceSelected(newSelectedState);
        // if deselecting
        if (!newSelectedState) {
            // deselect all of source's path points as well
            (*copy)[sourceIndex].setAllPathPointsSelected(false);
            // make a state snapshot for undo/redos
            saveCurrentState(1);
        }
        sources.update(copy);
        (*copy)[sourceIndex].doneUpdatingPath();
    }
}

// this function is intended to provide functionality for select all(control/cmd + a) and a click in space to deselect all
void ThreeDAudioProcessor::selectAllSources(const bool newSelectedState)
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        // if selecting
        if (newSelectedState) {
            for (auto& source : *copy) {
                // if a source is already selected, select its path points
                if (source.getSourceSelected()) {
                    source.setAllPathPointsSelected(true);
                } else { // otherwise just select the unselected source
                    source.setSourceSelected(newSelectedState);
                }
            }
        }
        // if deselecting
        else {
            // unselect all sources and their potentially selected path points
            for (auto& source : *copy) {
                source.setSourceSelected(newSelectedState);
                source.setAllPathPointsSelected(false);
            }
            // make a state snapshot for undo/redos
            saveCurrentState(1);
        }
        sources.update(copy);
        for (auto& source : *copy)
            source.doneUpdatingPath();
    }
}

bool ThreeDAudioProcessor::getSourceSelected(const std::size_t sourceIndex) const
{
    const Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy && sourceIndex < copy->size()) {
        return (*copy)[sourceIndex].getSourceSelected();
    }
    return false;
}

void ThreeDAudioProcessor::deleteSelectedSources()
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        bool doUndoableAction = false;
        for (int i = 0; i < copy->size(); ++i) {
            if ((*copy)[i].getSourceSelected()) {
                if (!doUndoableAction) {
                    saveCurrentState(-1);
                    doUndoableAction = true;
                }
                const int numPtsDeleted = (*copy)[i].deleteSelectedPathPoints();
                // no remaining path segments exist so we need to set the moving state back to sationary (otherwise problems used to occur)
                //if (sources[i]->getNumPathPoints() <= 1)
                //    sources[i]->setSourceMoving(false);
                // no path points or all points were selected and deleted so delete the source itself
                if (numPtsDeleted == 0 || (*copy)[i].getNumPathPoints() == 0) {
                    // need to mark these changes so that erase can work properly for the paths (due to operator= impl for SoundSource)
                    for (auto& source : *copy) {
                        source.setPathChanged(true);
                        source.setPathPosChanged(true);
                    }
                    copy->erase(copy->cbegin() + i);
                    --i;
                }
            }
        }
        if (doUndoableAction) {
            // erase() would have changed this a bit so again refresh the changed state of the paths so that updates to the undo state and other copies are made correctly
            for (auto& source : *copy) {
                source.setPathChanged(true);
                source.setPathPosChanged(true);
            }
            saveCurrentState(1);
            sources.update(copy);
            for (auto& source : *copy) {
                source.doneUpdatingPath();
                source.doneUpdatingPathPos();
            }
            pathChanged = true;
            pathPosChanged = true;
        }
    }
}

void ThreeDAudioProcessor::toggleLockSourcesToPaths()
{
    lockSourcesToPaths = !lockSourcesToPaths;
    if (!lockSourcesToPaths)
    {   // make sure to unmute any muted sources if unlocking
        Sources* copy = nullptr;
        const Locker lock (sources.get(copy));
        if (copy) {
            for (auto& source : *copy)
                source.setSourceMuted(false);
            sources.update(copy);
        }
    }
}

bool ThreeDAudioProcessor::getLockSourcesToPaths() const
{
    return lockSourcesToPaths;
}

void ThreeDAudioProcessor::toggleDoppler()
{
    dopplerOn = !dopplerOn;
    // TODO: detect largest latency of dopplers for each source and adjust (along with resampling) with setLatencySamples()
}

int ThreeDAudioProcessor::moveSelectedSourcesXYZ(const float dx, const float dy, const float dz, const bool moveSource)
{
    int movedStuff = 0;
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        for (auto& source : *copy) {
            // in this case we want to move the source
            if (source.getSourceSelected()) {
                // get the sources before first move
                saveCurrentState(0);
                // if any of the selected source's path points are selected, move only them
                if (!source.moveSelectedPathPointsXYZ(dx, dy, dz) || moveSource) {
                    // don't move sources if we are playing and sources are locked to paths
                    if (!(playing && lockSourcesToPaths && source.getNumPathPoints() > 1)) {
                        auto pos = source.getPosXYZ();
                        pos[0] += dx;
                        pos[1] += dy;
                        pos[2] += dz;
                        debug({NAMEANDVALUE(pos[0]), NAMEANDVALUE(pos[1]), NAMEANDVALUE(pos[2])}, PrintToFileMode::APPEND);
                        // bounds checking should be done by this func
                        source.setPosXYZ(pos);
                        cauto penis = source.getPosXYZ();
                        debug({NAMEANDVALUE(penis[0]), NAMEANDVALUE(penis[1]), NAMEANDVALUE(penis[2])}, PrintToFileMode::APPEND);
                        movedStuff = 1;
                    }
                }
                if (source.getNumSelectedPathPoints() > 0)
                    movedStuff = 1;
            }
        }
        if (movedStuff) {
            sources.update(copy);
            for (auto& source : *copy)
                source.doneUpdatingPath();
            pathChanged = true;
        }
    }
    return movedStuff;
}

int ThreeDAudioProcessor::moveSelectedSourcesRAE(const float dRad, const float dAzi, const float dEle, const bool moveSource)
{
    int movedStuff = 0;
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        for (auto& source : *copy) {
            // in this case we want to move the source
            if (source.getSourceSelected()) {
                // get sources before first move
                saveCurrentState(0);
                // if some, but not all, of the selected source's path points are selected, move only them
                if (!source.moveSelectedPathPointsRAE(dRad, dAzi, dEle) || moveSource) {
                    // don't move sources if we are playing and sources are locked to valid paths (with more than 1 pt)
                    if (!(playing && lockSourcesToPaths && source.getNumPathPoints() > 1)) {
                        // otherwise move the selected sources
                        auto pos = source.getPosRAE();
                        pos[0] *= dRad;
                        pos[1] += dAzi;
                        pos[2] += source.getEleDir() * dEle;
                        // bounds checking should be done by this func
                        source.setPosRAE(pos);
                        movedStuff = 1;
                    }
                }
                if (source.getNumSelectedPathPoints() > 0)
                    movedStuff = 1;
            }
        }
        if (movedStuff) {
            sources.update(copy);
            for (auto& source : *copy)
                source.doneUpdatingPath();
            pathChanged = true;
        }
    }
    return movedStuff;
}

void ThreeDAudioProcessor::dropPathPoint()
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        bool doUndoableAction = false;
        // drop a new path point for each selected source at the current position
        for (auto& source : *copy) {
            if (source.getSourceSelected()) {
                if (!doUndoableAction) {
                    saveCurrentState(-1);
                    doUndoableAction = true;
                }
                source.addPathPoint();
            }
        }
        if (doUndoableAction) {
            saveCurrentState(1);
            sources.update(copy);
            for (auto& source : *copy)
                source.doneUpdatingPath();
            pathChanged = true;
        }
    }
}

bool ThreeDAudioProcessor::dropPathPoint(const float (&xyz)[3])
{
    bool doUndoableAction = false;
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        // drop a new path point for each selected source at the specified position
        for (auto& source : *copy) {
            if (source.getSourceSelected()) {
                if (!doUndoableAction) {
                    saveCurrentState(-1);
                    doUndoableAction = true;
                }
                std::array<float, 3> pos {xyz[0],xyz[1],xyz[2]};
                source.addPathPoint(pos);
            }
        }
        if (doUndoableAction) {
            saveCurrentState(1);
            sources.update(copy);
            for (auto& source : *copy)
                source.doneUpdatingPath();
            pathChanged = true;
        }
    }
    return doUndoableAction;
}

void ThreeDAudioProcessor::togglePathPointSelected(const int sourceIndex, const int ptIndex)
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        const bool nextState = ! (*copy)[sourceIndex].getPathPointSelected(ptIndex);
        (*copy)[sourceIndex].setPathPointSelected(ptIndex, nextState);
        // if deselecting make a state snapshot for undo/redos
        if (!nextState)
            saveCurrentState(1);
        sources.update(copy);
        (*copy)[sourceIndex].doneUpdatingPath();
    }
}

void ThreeDAudioProcessor::setPathPointSelectedState(const int sourceIndex, const int ptIndex,
                                                     const bool newSelectedState)
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        (*copy)[sourceIndex].setPathPointSelected(ptIndex, newSelectedState);
        // if deselecting make a state snapshot for undo/redos
        if (!newSelectedState)
            saveCurrentState(1);
        sources.update(copy);
        (*copy)[sourceIndex].doneUpdatingPath();
    }
}

void ThreeDAudioProcessor::selectAllPathAutomationView(const bool newSelectedState)
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        bool anySelected = false;
        for (auto& source : *copy) {
            if (source.getSourceSelected()) {
                source.setAllPathAutomationPointsSelected(newSelectedState);
                anySelected = true;
            }
        }
        if (anySelected) {
            sources.update(copy);
            for (auto& source : *copy)
                source.doneUpdatingPathPos();
        }
    }
}

void ThreeDAudioProcessor::setPathAutomationPointSelectedState(const int sourceIndex, const int ptIndex,
                                                               const bool newSelectedState)
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        (*copy)[sourceIndex].setPathAutomationPointSelected(ptIndex, newSelectedState);
        // if deselecting make a state snapshot for undo/redos
        if (!newSelectedState)
            saveCurrentState(1);
        sources.update(copy);
        (*copy)[sourceIndex].doneUpdatingPathPos();
    }
}

void ThreeDAudioProcessor::togglePathAutomationPointSelected(const int sourceIndex, const int ptIndex)
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        const bool nextState = ! (*copy)[sourceIndex].getPathAutomationPointSelected(ptIndex);
        (*copy)[sourceIndex].setPathAutomationPointSelected(ptIndex, nextState);
        // if deselecting make a state snapshot for undo/redos
        if (!nextState)
            saveCurrentState(1);
        sources.update(copy);
        (*copy)[sourceIndex].doneUpdatingPathPos();
    }
}

void ThreeDAudioProcessor::deselectAllPathAutomationPoints()
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        for (auto& source : *copy)
            source.setAllPathAutomationPointsSelected(false);
        // make a state snapshot for undo/redos
        saveCurrentState(1);
        sources.update(copy);
        for (auto& source : *copy)
            source.doneUpdatingPathPos();
    }
}

// return value: = 0 no pts selected/moved, > 0 pts moved as desired, < 0 pts maybe moved but not completely as desired due to bounds constraints applied to the entire selected group
int ThreeDAudioProcessor::moveSelectedPathAutomationPoints(float dx, float dy)
{
    // if we can't make the full position change due to bounds constraints on the entire selected group of points, then sign goes negative
    int sign = 1, numMoved = 0;
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        // bounds check dx and dy so all selected path auto pts are moved as a group and all stay within bounds
        for (auto& source : *copy) {
            // if source is selected (its path auto pts are visible on path auto screen)
            if (source.getSourceSelected()) {
                // get sources before first move
                saveCurrentState(0);
                std::vector<std::vector<float>> pts = source.getPathPosPtr()->getSelectedPoints();
                float newX, newY;
                for (int i = 0; i < pts.size(); ++i) {
                    newX = pts[i][0] + dx;
                    newY = pts[i][1] + dy;
                    if (newX < 0) {
                        dx -= newX;
                        sign = -1;
                    }
                    if (newY < 0) {
                        dy -= newY;
                        sign = -1;
                    }
                    if (newY > 1) {
                        dy -= newY-1;
                        sign = -1;
                    }
                }
            }
        }
        // now that we are bounds checked for the selected group, do the moving
        for (auto& source : *copy) {
            if (source.getSourceSelected())
                numMoved += source.moveSelectedPathAutomationPoints(dx, dy);
        }
        if (numMoved) {
            sources.update(copy);
            for (auto& source : *copy)
                source.doneUpdatingPathPos();
            pathPosChanged = true;
        }
    }
    return sign*numMoved;
}

void ThreeDAudioProcessor::moveSelectedPathAutomationPointsTo(const int referencePtSourceIndex,
                                                              int& referencePtIndex,
                                                              const int referencePtIndexAmongSelecteds,
                                                              const float x, const float y)
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        const std::vector<float> refPt = std::get<0>((*copy)[referencePtSourceIndex].getPathPosPtr()
                                                     ->getSelectedPoint(referencePtIndexAmongSelecteds));
        moveSelectedPathAutomationPoints(x - refPt[0], y - refPt[1]);
        referencePtIndex = std::get<1>((*copy)[referencePtSourceIndex].getPathPosPtr()
                                       ->getSelectedPoint(referencePtIndexAmongSelecteds));
        // sources.update(), doneUpdatingPathPos(), and pathPosChanged = true should be performed in moveSelectedPathAutomationPoints()
    }
}

void ThreeDAudioProcessor::addPathAutomationPtAtXY(const float (&xy)[2])
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        bool doUndoableAction = false;
        for (auto& source : *copy) {
            if (source.getSourceSelected()) {
                if (!doUndoableAction) {
                    saveCurrentState(-1);
                    doUndoableAction = true;
                }
                source.addPathAutomationPoint(xy[0], xy[1]);
            }
        }
        if (doUndoableAction) {
            saveCurrentState(1);
            sources.update(copy);
            for (auto& source : *copy)
                source.doneUpdatingPathPos();
            pathPosChanged = true;
        }
    }
}

void ThreeDAudioProcessor::deleteSelectedAutomationPoints()
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        bool doUndoableAction = false;
        for (auto& source : *copy) {
            if (source.getPathPosPtr()->getNumSelectedPoints()) {
                saveCurrentState(-1);
                doUndoableAction = true;
            }
            source.deleteSelectedAutomationPoints();
        }
        if (doUndoableAction) {
            saveCurrentState(1);
            sources.update(copy);
            for (auto& source : *copy)
                source.doneUpdatingPathPos();
            pathPosChanged = true;
        }
    }
}

void ThreeDAudioProcessor::setSelectedPathAutomationPointsSegmentType(const int newSegType)
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        bool doUndoableAction = false;
        for (auto& source : *copy) {
            // get the sources before edit
            if (source.getPathPosPtr()->getSelectedSplines().size()) {
                saveCurrentState(-1);
                doUndoableAction = true;
            }
            source.setSelectedPathAutomationPointsSegmentType(newSegType);
        }
        if (doUndoableAction) {
            saveCurrentState(1);
            sources.update(copy);
            for (auto& source : *copy)
                source.doneUpdatingPathPos();
            pathPosChanged = true;
        }
    }
}

void ThreeDAudioProcessor::toggleSelectedSourcesPathType()
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        bool doUndoableAction = false;
        for (auto& source : *copy) {
            if (source.getSourceSelected() && source.getPathPtr() != nullptr) {
                // get the sources before edit
                if (!doUndoableAction) {
                    saveCurrentState(-1);
                    doUndoableAction = true;
                }
                int pathType = (int)(source.getPathPtr()->getType());
                pathType += 1;
                if (pathType > 1)
                    pathType -= 2;
                source.setPathType(pathType);
            }
        }
        if (doUndoableAction) {
            saveCurrentState(1);
            sources.update(copy);
            for (auto& source : *copy)
                source.doneUpdatingPath();
            pathChanged = true;
        }
    }
}

std::vector<std::vector<float>> ThreeDAudioProcessor::getPathPoints(const int sourceIndex) const
{
    const Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        return (*copy)[sourceIndex].getPathPoints();
    }
    return std::vector<std::vector<float>>();
}

std::vector<bool> ThreeDAudioProcessor::getPathPointsSelected(const int sourceIndex) const
{
    const Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy && 0 <= sourceIndex && sourceIndex < copy->size()) {
        return (*copy)[sourceIndex].getPathPtr()->getPointsSelected();
    }
    return std::vector<bool>();
}

bool ThreeDAudioProcessor::getPathPointSelected(const std::size_t sourceIndex,
                                                const std::size_t pathPointIndex) const
{
    const Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy && sourceIndex < copy->size() && pathPointIndex < (*copy)[sourceIndex].getPathPtr()->getNumPoints()) {
        return (*copy)[sourceIndex].getPathPtr()->getPointSelected(pathPointIndex);
    }
    return false;
}

void ThreeDAudioProcessor::setSelectedPathPointIndecies(cint sourceIndex,
                                                        cint pathPointIndex,
                                                        cint newIndex)
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {// && sourceIndex < copy->size() && pathPointIndex < (*copy)[sourceIndex].getPathPtr()->getNumPoints()) {
        auto path = (*copy)[sourceIndex].getPathPtrMutable();
        if (path->getNumSelectedPoints() > 0) {
            saveCurrentState(-1);
            path->setSelectedPointIndices(pathPointIndex, newIndex);
            saveCurrentState(1);
            sources.update(copy);
        }
    }
}

//void ThreeDAudioProcessor::markPathAsUpdated(const int sourceIndex)
//{
//    const ScopedLock lockSources (sources.getLock());
//    sources[sourceIndex]->setPathChangedState(false);
//}
//
//void ThreeDAudioProcessor::markPathPosAsUpdated(const int sourceIndex)
//{
//    const ScopedLock lockSources (sources.getLock());
//    sources[sourceIndex]->setPathPosChangedState(false);
//}

void ThreeDAudioProcessor::copySelectedPathAutomationPoints()
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        bool didIt = false;
		for (int s = 0; s < copy->size(); ++s) {
			if ((*copy)[s].getSourceSelected()) {
				if ((*copy)[s].getPathPosPtr()->getPointsSelected().size() > 0) {
					saveCurrentState(1);
					saveCurrentState(0);
					didIt = true;
				}
				(*copy)[s].copySelectedPathAutomationPoints();
			}
		}
        /*for (auto& source : (*copy)) { // windows throws an exception that vector iterator can't be incremented if vector size is 1
            if (source.getSourceSelected()) {
                if (source.getPathPosPtr()->getPointsSelected().size() > 0) {
                    saveCurrentState(1);
                    saveCurrentState(0);
                    didIt = true;
                }
                source.copySelectedPathAutomationPoints();
            }
        }*/
        if (didIt) {
            sources.update(copy);
            for (auto& source : *copy)
                source.doneUpdatingPathPos();
            pathPosChanged = true;
        }
    }
}

std::vector<std::vector<float>> ThreeDAudioProcessor::getPathAutomationPoints(const int sourceIndex) const
{
    const Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        return (*copy)[sourceIndex].getPathPosPtr()->getPoints();
    }
    return std::vector<std::vector<float>>();
}

//std::vector<std::vector<float>> ThreeDAudioProcessor::getSelectedPathAutomationPoints(const int sourceIndex)
//{
//    const ScopedLock lockSources (sources.getLock());
//    return sources[sourceIndex]->getPathPosPtr()->getSelectedPoints();
//}

//std::vector<bool> ThreeDAudioProcessor::getPathAutomationPointsSelected(const int sourceIndex)
//{
//    const ScopedLock lockSources (sources.getLock());
//    return sources[sourceIndex]->getPathPosPtr()->getPointsSelected();
//}

int ThreeDAudioProcessor::getPathAutomationPointIndexAmongSelectedPoints(const int sourceIndex, const int pointIndex) const
{
    const Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        const std::vector<float> thePoint = (*copy)[sourceIndex].getPathPosPtr()->getPoint(pointIndex);
        const std::vector<std::vector<float>> selectedPoints = (*copy)[sourceIndex].getPathPosPtr()->getSelectedPoints();
        int i = 0;
        for (const auto& pt : selectedPoints) {
            if (pt == thePoint)
                return i;
            ++i;
        }
    }
    return -1; // failed to find the relative index
}

bool ThreeDAudioProcessor::areAnySelectedSourcesPathAutomationPointsSelected() const
{
    const Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        for (const auto& source : *copy) {
            if (source.getSourceSelected()) {
                const std::vector<bool> pointsSelected = source.getPathPosPtr()->getPointsSelected();
                if (std::find(pointsSelected.cbegin(), pointsSelected.cend(), true) != pointsSelected.cend()) {
                    return true;
                }
            }
        }
    }
    return false;
}

void ThreeDAudioProcessor::makeSourcesVisibleForPathAutomationView()
{
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        // see if any sources are selected
        bool noSourcesSelected = true;
        for (const auto& source : *copy) {
            if (source.getSourceSelected()) {
                noSourcesSelected = false;
                break;
            }
        }
        // if the user didn't select any explicitly, just automatically select all the sources for automating
        if (noSourcesSelected) {
            for (auto& source : *copy)
                source.setSourceSelected(true);
            sources.update(copy);
        }
    }
}

void ThreeDAudioProcessor::resetPlaying(const float frameRate) noexcept
{
    if (playing) {
        ++resetPlayingCount;
        if (resetPlayingCount > 0.5f * frameRate) // resets playing if processBlock not called in last 1/2 sec
            playing = false;
    }
}

void ThreeDAudioProcessor::toggleLooping(const float defaultBegin,
                                         const float defaultEnd)
{
    float begin = -1, end = -1;
    cauto prevBegin = loopRegionBegin.load();
    cauto prevEnd = loopRegionEnd.load();
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        float firstSelectedXPos, lastSelectedXPos;
        for (const auto& source : *copy) {
            if (source.getSourceSelected()) {
                auto pathPos = source.getPathPosPtr();
                cauto numSelectedPoints = pathPos->getNumSelectedPoints();
                if (numSelectedPoints > 0) {
                    firstSelectedXPos = std::get<0>(pathPos->getSelectedPoint(0))[0];
                    if (begin == -1 || firstSelectedXPos < begin)
                        begin = firstSelectedXPos;
                    if (end == -1 || firstSelectedXPos > end)
                        end = firstSelectedXPos;
                }
                if (numSelectedPoints > 1) {
                    lastSelectedXPos = std::get<0>(pathPos->getSelectedPoint(numSelectedPoints-1))[0];
                    if (begin == -1 || lastSelectedXPos < begin)
                        begin = lastSelectedXPos;
                    if (end == -1 || lastSelectedXPos > end)
                        end = lastSelectedXPos;
                }
            }
        }
    }
    if (begin != -1 && end != -1 && begin != end) {
        loopRegionBegin = begin;
        loopRegionEnd = end;
    }
    if (defaultBegin > loopRegionEnd || defaultEnd < loopRegionBegin) {
        loopRegionBegin = defaultBegin;
        loopRegionEnd = defaultEnd;
    }
    if (loopRegionBegin != prevBegin || loopRegionEnd != prevEnd)
        loopingEnabled = true;
    else
        loopingEnabled = !loopingEnabled;
}

// defines a time region to loop over based on the first and last selected path auto points for all selected sources.  if less than two pts are selected, turn off looping
void ThreeDAudioProcessor::defineLoopingRegionUsingSelectedPathAutomationPoints()
{
    float begin = -1, end = -1;
    Sources* copy = nullptr;
    const Locker lock (sources.get(copy));
    if (copy) {
        float firstSelectedXPos, lastSelectedXPos;
        for (const auto& source : *copy) {
            if (source.getSourceSelected()) {
                auto pathPos = source.getPathPosPtr();
                const int numSelectedPoints = pathPos->getNumSelectedPoints();
                if (numSelectedPoints > 0) {
                    firstSelectedXPos = std::get<0>(pathPos->getSelectedPoint(0))[0];
                    if (begin == -1 || firstSelectedXPos < begin)
                        begin = firstSelectedXPos;
                    if (end == -1 || firstSelectedXPos > end)
                        end = firstSelectedXPos;
                }
                if (numSelectedPoints > 1) {
                    lastSelectedXPos = std::get<0>(pathPos->getSelectedPoint(numSelectedPoints-1))[0];
                    if (begin == -1 || lastSelectedXPos < begin)
                        begin = lastSelectedXPos;
                    if (end == -1 || lastSelectedXPos > end)
                        end = lastSelectedXPos;
                }
            }
        }
    }
    if (begin != -1 && end != -1 && begin != end) {
        // if the looping region is the exact same as it was toggle the looping off by reseting back to -1's
        if (begin == loopRegionBegin && end == loopRegionEnd) {
            loopRegionBegin = -1;
            loopRegionEnd = -1;
        } else { // otherwise set the new region as desired
            loopRegionBegin = begin;
            loopRegionEnd = end;
        }
    } else {
        loopRegionBegin = -1;
        loopRegionEnd = -1;
    }
}

//void ThreeDAudioProcessor::setSources(const Lockable<Sources>& newSources)
//{
//    // update sources from an undo/redo state
//    const Locker lock (newSources.getLock());
//    sources.update(&newSources.getResource());
//    pathChanged = true;
//    pathPosChanged = true;
//    // might get an empty screen for automation view if we don't do this
//    if (displayState == DisplayState::PATH_AUTOMATION)
//        makeSourcesVisibleForPathAutomationView();
//}
void ThreeDAudioProcessor::setSources(const Sources& newSources)
{
    // update sources from an undo/redo state
    sources.update(&newSources);
    pathChanged = true;
    pathPosChanged = true;
    // might get an empty screen for automation view if we don't do this
    if (displayState == DisplayState::PATH_AUTOMATION)
        makeSourcesVisibleForPathAutomationView();
}

void ThreeDAudioProcessor::setSpeedOfSound(const float newSpeedOfSound)
{
    speedOfSound = newSpeedOfSound;
}

void ThreeDAudioProcessor::setProcessingMode(const ProcessingMode newMode) noexcept
{
    processingMode = newMode;
    if (processingMode == ProcessingMode::AUTO_DETECT)
        realTime.store(isHostRealTime);
    else
        realTime = (processingMode == ProcessingMode::REALTIME);
}

std::string ThreeDAudioProcessor::getCurrentTimeString(const int opt) const
{
    switch (opt)
    {
        case 0: // time in sec
            return std::to_string(posSEC);
        case 1:
            const float den = timeSigDen * 0.25;
            const int meas = std::floor(bpm / 60.0 * posSEC / timeSigNum * den) + 1.0;
            const float beat = std::fmod(bpm / 60.0f * posSEC * den, timeSigNum) + 1.0;
            const float frac = (beat - std::floor(beat)); /// 0.25;
            return std::to_string(meas) + " | " + std::to_string(static_cast<int>(beat)) + " | " + std::to_string(frac);
    }
    return std::string();
}

std::tuple<int, int, float> ThreeDAudioProcessor::getMeasuresBeatsFrac(const float sec) const
{
    const float den = timeSigDen * 0.25;
    const int meas = std::floor(bpm / 60.0 * sec / timeSigNum * den) + 1.0;
    const float beat = std::fmod(bpm / 60.0f * sec * den, timeSigNum) + 1.0;
    const float frac = (beat - std::floor(beat)); /// 0.25;
    return std::make_tuple(meas, (int)beat, frac);
}

// JUCE auto-generated stuff
//==============================================================================
const String ThreeDAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

//int ThreeDAudioProcessor::getNumParameters()
//{
//    return 0;
//}
//
//float ThreeDAudioProcessor::getParameter (int index)
//{
//    return 0.0f;
//}
//
//void ThreeDAudioProcessor::setParameter (int index, float newValue)
//{
//}
//
//const String ThreeDAudioProcessor::getParameterName (int index)
//{
//    return String::empty;
//}
//
//const String ThreeDAudioProcessor::getParameterText (int index)
//{
//    return String::empty;
//}

const String ThreeDAudioProcessor::getInputChannelName (int channelIndex) const
{
    return String (channelIndex + 1);
}

const String ThreeDAudioProcessor::getOutputChannelName (int channelIndex) const
{
    return String (channelIndex + 1);
}

bool ThreeDAudioProcessor::isInputChannelStereoPair (int index) const
{
    return true;
}

bool ThreeDAudioProcessor::isOutputChannelStereoPair (int index) const
{
    return true;
}

bool ThreeDAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool ThreeDAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool ThreeDAudioProcessor::silenceInProducesSilenceOut() const
{
    return false;
}

double ThreeDAudioProcessor::getTailLengthSeconds() const
{
    // not sure if the tail length reported here should include latency due to resampling
    if (fs != sampleRate_HRTF) {
        return (numTimeSteps+1.0) / getSampleRate();
    } else {
        return numTimeSteps / getSampleRate();
    }
}

int ThreeDAudioProcessor::getNumPrograms()
{
    return 1;
}

int ThreeDAudioProcessor::getCurrentProgram()
{
    return 0;
}

void ThreeDAudioProcessor::setCurrentProgram (int index)
{
}

const String ThreeDAudioProcessor::getProgramName (int index)
{
    return String::empty;
}

void ThreeDAudioProcessor::changeProgramName (int index, const String& newName)
{
}

void ThreeDAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    N = samplesPerBlock;
    if (fs != sampleRate) {
        fs = sampleRate;
        // set doppler(s) to the new sample rate, reallocation for this change happens in allocateForMaxBufferSize() below
        for (auto& s : playableSources)
            s.setDopplerSampleRate(fs);
        // TODO: detect largest latency of doppler and factor that in to the setLatencySamples() call below
    }
    int playableSourceMaxBufferSize = N;
    if (fs != sampleRate_HRTF) {
        resampler = Resampler(fs, N, sampleRate_HRTF, true);
        unsamplerCh1 = Resampler(sampleRate_HRTF, N, fs, false);
        unsamplerCh2 = Resampler(sampleRate_HRTF, N, fs, false);
        setLatencySamples(1);
        playableSourceMaxBufferSize = resampler.getNoutMax();
    }
//    {
//        sources.load(std::vector<SoundSource>(1));
//        if (displayState == DisplayState::PATH_AUTOMATION)
//            makeSourcesVisibleForPathAutomationView();
////        Sources* copy = nullptr;
////        const Locker lock (sources.get(copy));
////        if (copy) {
////            if (copy->size() == 0) {
////                // add a source to get started
////                copy->emplace_back(std::array<float, 3>{1, 0, 0});
////                sources.update(copy);
////            }
////        }
//    }
    // update all the real time state
    isHostRealTime = !isNonRealtime();
    realTime = (processingMode == ProcessingMode::AUTO_DETECT) ? isHostRealTime.load() : processingMode == ProcessingMode::REALTIME;
    // allocate space in each PlayableSoundSource for processing
    for (auto& s : playableSources) {
        s.allocateForMaxBufferSize(playableSourceMaxBufferSize);
    }
    // now we are setup for processing
    inited = true;
}

void ThreeDAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

void ThreeDAudioProcessor::processBlock (AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
    // In case we have more outputs than inputs, we'll clear any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear(i, 0, buffer.getNumSamples());
    
    // if the plugin is initialized by prepareToPlay()
    if (inited)
    {
        // need to update block size if it is not what we expected to make sure we have enough memory alloced for processing
        if (N != buffer.getNumSamples())
        {
            N = buffer.getNumSamples();
            int playableSourceMaxBufferSize = N;
            // also got to reset the resampler to the new buffer size if the incoming sample rate is not 44.1kHz
            if (fs != sampleRate_HRTF)
            {
                resampler = Resampler(fs, N, sampleRate_HRTF, true);
                unsamplerCh1 = Resampler(sampleRate_HRTF, N, fs, false);
                unsamplerCh2 = Resampler(sampleRate_HRTF, N, fs, false);
                playableSourceMaxBufferSize = resampler.getNoutMax();
            }
            for (auto& s : playableSources)
                s.allocateForMaxBufferSize(playableSourceMaxBufferSize);
        }
        
        // update playback position stuff
        AudioPlayHead::CurrentPositionInfo positionInfo;
        // apparently you only want to call this inside this process block and the information returned by it is only valid for the current process block.
        getPlayHead()->getCurrentPosition(positionInfo);
        //gPositionInfo = positionInfo;
        positionInfo.timeInSeconds = positionInfo.timeInSamples / fs; // timeInSeconds is always 0 in Tracktion all of a sudden, WTF?!?
        playing = positionInfo.isPlaying;
        timeSigNum = positionInfo.timeSigNumerator;
        timeSigDen = positionInfo.timeSigDenominator;
        bpm = positionInfo.bpm;
        resetPlayingCount = 0;
        bool looped = false;
        const float thisBufferDuration = ((float)N)/fs;
        if (loopingEnabled /*loopRegionBegin >= 0 && loopRegionEnd >= 0*/)
        {
            // if the current playback position follows the previous, increment the plugin's playback position without the modulo operation so changing the looping region will not cause craziness
            if (playing && std::abs(posSECPrevHost+thisBufferDuration - positionInfo.timeInSeconds) < thisBufferDuration) {
                posSEC = posSECprev + thisBufferDuration;
                // keep within the looping region
                if (posSEC < loopRegionBegin)
                    posSEC.store(loopRegionBegin.load());
                if (posSEC >= loopRegionEnd)
                    posSEC = loopRegionBegin + posSEC - loopRegionEnd;
            }
            // if the playback position has jumped to somewhere else, reset the plugin's playback position via modulo by the looping region length
            else {
                posSEC = loopRegionBegin + std::fmod(positionInfo.timeInSeconds, loopRegionEnd - loopRegionBegin);
            }
            // check to see if we looped
            if (posSECprev < loopRegionEnd && posSECprev+thisBufferDuration >= loopRegionEnd)
                looped = true;
        }
        else
            posSEC = positionInfo.timeInSeconds;
        
        // if the playback position does not immediately follow the previous one and it wasn't caused by the looping feature, need to reset the doppler buffer state so that no old audio remaining is played back at the new position
        bool resetProcessingState = false;
        if (!looped && std::abs(posSECprev+thisBufferDuration - posSEC) > thisBufferDuration)
            resetProcessingState = true;
        posSECprev = posSEC;
        posSECPrevHost = positionInfo.timeInSeconds;
        
        // convert possibly multiple input channels to mono
        const int numChannels = buffer.getNumChannels();
        const int currentN = N;
        STACK_ARRAY(float, input, currentN);
        STACK_ARRAY(float, stereoInput, 2*currentN);
//    #ifdef WIN32
//        float *input = static_cast<float *>(alloca(currentN * sizeof(float)));
//    #else
//        float input[currentN];
//    #endif
        for (int n = 0; n < currentN; ++n)
            input[n] = 0;
        const float scale = 1.0 / numChannels;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            for (int n = 0; n < currentN; ++n) {
                input[n] += buffer.getSample(ch, n) * scale;
                stereoInput[ch*currentN + n] = buffer.getSample(ch, n);
            }
            // clear buffer after grabing a local copy of the input and before the heavier processing goes on to avoid garbage output should the processing not finish in time
            buffer.clear(ch, 0, currentN);
        }
        
        // got to resample to 44.1kHz if input is a different sample rate because the HRIR data is 44.1kHz
        // NOTE: size is getNoutMax() b/c we can't tell if the buffer will be long or short until we make the resample call below
        const int resampledMaxSize = resampler.getNoutMax();
        STACK_ARRAY(float, inputResampled, resampledMaxSize)
//    #ifdef WIN32
//        float *inputResampled = static_cast<float *>(alloca(resampledMaxSize * sizeof(float)));
//    #else
//        float inputResampled[resampledMaxSize];
//    #endif
        if (fs != sampleRate_HRTF)
        {
            for (int n = 0; n < resampledMaxSize; ++n)
                inputResampled[n] = 0;
            resampler.resampleLinear(&input[0], &inputResampled[0]);
        }
        
        // the net output accumulator for all sources
        const int outputSize = 2*currentN;
        STACK_ARRAY(float, output, outputSize)
//    #ifdef WIN32
//        float *output = static_cast<float *>(alloca(outputSize * sizeof(float)));
//    #else
//        float output[outputSize];
//    #endif
        for (int n = 0; n < outputSize; ++n)
            output[n] = 0;
        
        // the resampled version ...
        const int resampledNout = resampler.getNout();
        const int resampledSize = 2*resampledNout;
        STACK_ARRAY(float, outputResampled, resampledSize);
//    #ifdef WIN32
//        float *outputResampled = static_cast<float *>(alloca(resampledSize * sizeof(float)));
//    #else
//        float outputResampled[resampledSize];
//    #endif
        for (int n = 0; n < resampledSize; ++n)
            outputResampled[n] = 0;
        
        float *inputPtr, *outputPtr;
        int inputLength;
        if (fs != sampleRate_HRTF)
        {
            inputPtr = &inputResampled[0];
            outputPtr = &outputResampled[0];
            inputLength = resampledNout;
        }
        else
        {
            inputPtr = &input[0];
            outputPtr = &output[0];
            inputLength = currentN;
        }
        
        // process the sources
        {
            Sources* copy = nullptr;
            const std::unique_lock<Mutex> lock (sources.get(copy), std::try_to_lock);
            //const Locker lock (sources.get(copy));
            if (lock.owns_lock() && copy) {
                //bool setSourcePosFromPath = false;
                for (int s = 0; s < (const int)copy->size(); ++s)
                {   // update the moving source position here for those sources automated on a path
                    //setSourcePosFromPath = false;
                    if (lockSourcesToPaths && playing)
                        /*setSourcePosFromPath =*/ (*copy)[s].setParametricPosition(posSEC, playableSources[s].prevPathPosIndex, *sourcePathPositionsFromDAW[s]);
                    // serves as a single point of update for the positional state to ensure positional continuity btw buffers
                    playableSources[s].updateFromSoundSource((*copy)[s]);
                    playableSources[s].setDopplerOn(dopplerOn /*&& setSourcePosFromPath*/, speedOfSound);
                    if (resetProcessingState)
                        playableSources[s].resetProcessingState();
                    if (! playableSources[s].getSourceMuted())
                        playableSources[s].processAudio(inputPtr, inputLength, outputPtr, realTime);
                }
                prevSourcesSize = copy->size();
                sources.tryToUpdate(copy);
            } else { // failed to get the lock, so just use the previous PlayableSoundSource data to process this buffer
                for (int s = 0; s < (const int)prevSourcesSize; ++s)
                {   // compute approximated position if the source was previously moving since we don't have access to the interps of the locked source.  this is crucial to avoid glitches with the dopper effect on, not so important without the doppler as the ocassional glitches aren't noticable
                    playableSources[s].advancePosition();
                    if (! playableSources[s].getSourceMuted())
                        playableSources[s].processAudio(inputPtr, inputLength, outputPtr, realTime);
                }
            }
        }
        
        // resample the processed audio back to the original sample rate of the buffer given to us
        if (fs != sampleRate_HRTF)
        {
            unsamplerCh1.unsampleLinear(outputResampled, resampledNout, output);
            unsamplerCh2.unsampleLinear(&outputResampled[resampledNout], resampledNout, &output[currentN]);
        }
        
        // copy final data to output buffer
		for (int ch = 0; ch < 2; ++ch) 
			buffer.copyFrom(ch, 0, &output[ch*currentN], currentN, 0.18f * wetOutputVolume); // 0.18 scales the volume to about the input volume for the default single source in front of the listener at rae coord (1,0,0)

        if (dryOutputVolume > 0)
            for (int ch = 0; ch < 2; ++ch)
                for (int n = 0; n < currentN; ++n)
                    *buffer.getWritePointer(ch, n) += stereoInput[ch*currentN + n] * dryOutputVolume;
        
    } // end if inited
}

//==============================================================================
bool ThreeDAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

AudioProcessorEditor* ThreeDAudioProcessor::createEditor()
{
	ThreeDAudioProcessorEditor* editor = new ThreeDAudioProcessorEditor (this);
    return editor;
}

//==============================================================================
void ThreeDAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    
    // Here's an example of how you can use XML to make it easy and more robust:
    
    // Create an outer XML element..
    XmlElement xml ("ThreeDAudioPluginSettings");
    
    // add some attributes to it..
//    // not necessary and probably annoying to save window size with a plugin setting
//    xml.setAttribute ("uiWidth", lastUIWidth);
//    xml.setAttribute ("uiHeight", lastUIHeight);
    xml.setAttribute("dopplerOn", dopplerOn);
    xml.setAttribute("speedOfSound", speedOfSound);
    xml.setAttribute("loopRegionBegin", loopRegionBegin);
    xml.setAttribute("loopRegionEnd", loopRegionEnd);
    xml.setAttribute("loopingEnabled", loopingEnabled);
    xml.setAttribute("processingMode", (int)processingMode.load());
    xml.setAttribute("wetOutputVolume", wetOutputVolume.load());
    xml.setAttribute("dryOutputVolume", dryOutputVolume.load());
    // add all the data from the sources array
    {
        Sources* copy = nullptr;
        const Locker lock (sources.get(copy));
        if (copy) {
            for (const auto& source : *copy)
                xml.addChildElement(source.getXML());
        }
    }
    // then use this helper function to stuff it into the binary blob and return it..
    copyXmlToBinary (xml, destData);
}

void ThreeDAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    // This getXmlFromBinary() helper function retrieves our XML from the binary blob..
    const ScopedPointer<XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr)
    {
        // make sure that it's actually our type of XML object..
        if (xmlState->hasTagName("ThreeDAudioPluginSettings"))
        {
            // ok, now pull out our parameters..
//            // not necessary and probably annoying to save window size with a plugin setting
//            lastUIWidth  = xmlState->getIntAttribute ("uiWidth", lastUIWidth);
//            lastUIHeight = xmlState->getIntAttribute ("uiHeight", lastUIHeight);
            dopplerOn = xmlState->getBoolAttribute("dopplerOn", false);
            speedOfSound = xmlState->getDoubleAttribute("speedOfSound", defaultSpeedOfSound);
            loopRegionBegin = xmlState->getDoubleAttribute("loopRegionBegin", -1.0);
            loopRegionEnd = xmlState->getDoubleAttribute("loopRegionEnd", -1.0);
            loopingEnabled = xmlState->getBoolAttribute("loopingEnabled", false);// loopRegionBegin != -1 && loopRegionEnd != -1;
            setProcessingMode((ProcessingMode)xmlState->getIntAttribute("processingMode", 2));
            wetOutputVolume = xmlState->getDoubleAttribute("wetOutputVolume", 1.0);
            dryOutputVolume = xmlState->getDoubleAttribute("dryOutputVolume", 0.0);
            // restore all the saved sources and their state stuff
            saveCurrentState(-1);
            {
                Sources* copy = nullptr;
                const Locker lock (sources.get(copy));
                if (copy) {
                    copy->clear();
                    for (int s = 0; s < xmlState->getNumChildElements(); ++s)
                        copy->emplace_back(xmlState->getChildElement(s));
                    sources.update(copy);
                    pathChanged = true;
                    pathPosChanged = true;
                    presetJustLoaded = true;
                }
                // a newly loaded preset doesn't update visually for the PATH_AUTOMATION view if we don't do this...
                if (displayState == DisplayState::PATH_AUTOMATION)
                    makeSourcesVisibleForPathAutomationView();
            }
            saveCurrentState(1);
            
        }
    }
//    // update the editor with the new window size loaded from the settings
//    // not necessary and probably annoying to save window size with a plugin setting
//    if (editor != nullptr) {
//        editor->setSize(lastUIWidth, lastUIHeight);
//    }
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ThreeDAudioProcessor();
}


//// PRE CONCURRENTRESOURCE
//#include "PluginProcessor.h"
//
//#include "PluginEditor.h"
//#include "Data.h"
//
//// the global hrir data that gets one instance across multiple plugin instances
//float***** HRIRdata;
//float****  HRIRdataPoles;
//
////==============================================================================
//ThreeDAudioProcessor::ThreeDAudioProcessor()
//{
//    // if there are no instances going, load the global hrir data for all possible future instances
//    if (numRefs == 0) {
//        // unified poles, compact data
//        // binary hrtf file name
//        String path = File::getSpecialLocation(File::currentApplicationFile).getParentDirectory().getFullPathName();
//        path += "/3dAudioDataUnifiedPoles.bin";
//        // basic read (should be cross platform)
//        // open the stream
//        std::ifstream is(path.getCharPointer());
//        if (is.good()) {
//            // determine the file length
//            is.seekg(0, is.end);
//            size_t size = is.tellg();
//            is.seekg(0, is.beg);
//            // create a temp array to store the data
//            char* dataBuffer = new char[size];
//            // load the data
//            is.read(dataBuffer, size);
//            // close the file
//            is.close();
//            // pack data into HRIR array
//            int index = 0;
//            HRIRdata = new float****[numDistanceSteps];
//            for (int d = 0; d < numDistanceSteps; ++d) {
//                HRIRdata[d] = new float***[numAzimuthSteps/2+1];
//                for (int a = 0; a < numAzimuthSteps/2+1; ++a) {
//                    HRIRdata[d][a] = new float**[numElevationSteps-1];
//                    for (int e = 0; e < numElevationSteps-1; ++e) {
//                        HRIRdata[d][a][e] = new float*[2];
//                        for (int ch = 0; ch < 2; ++ch) {
//                            HRIRdata[d][a][e][ch] = new float[numTimeSteps];
//                            for (int t = 0; t < numTimeSteps; ++t) {
//                                HRIRdata[d][a][e][ch][t] = *((float*) &dataBuffer[index*sizeof(float)]);
//                                ++index;
//                            }
//                        }
//                    }
//                }
//            }
//            // pole data is the same for both channels
//            HRIRdataPoles = new float***[numDistanceSteps];
//            for (int d = 0; d < numDistanceSteps; ++d) {
//                HRIRdataPoles[d] = new float**[2];
//                HRIRdataPoles[d][0] = new float*[2]; // ele = 0 pole
//                HRIRdataPoles[d][1] = new float*[2]; // ele = 180 pole
//                //for (int ch = 0; ch < 2; ++ch) {
//                HRIRdataPoles[d][0][0] = new float[numTimeSteps];
//                HRIRdataPoles[d][0][1] = new float[numTimeSteps];
//                for (int t = 0; t < numTimeSteps; ++t) {
//                    HRIRdataPoles[d][0][1][t] = HRIRdataPoles[d][0][0][t] = *((float*) &dataBuffer[index*sizeof(float)]);
//                    ++index;
//                }
//                HRIRdataPoles[d][1][0] = new float[numTimeSteps];
//                HRIRdataPoles[d][1][1] = new float[numTimeSteps];
//                for (int t = 0; t < numTimeSteps; ++t) {
//                    HRIRdataPoles[d][1][1][t] = HRIRdataPoles[d][1][0][t] = *((float*) &dataBuffer[index*sizeof(float)]);
//                    ++index;
//                }
//                //}
//                //                HRIRdataPoles[d][0] = new float[numTimeSteps]; // ele = 0 pole
//                //                for (int t = 0; t < numTimeSteps; ++t) {
//                //                    HRIRdataPoles[d][0][t] = *((float*) &dataBuffer[index*sizeof(float)]);
//                //                    ++index;
//                //                }
//                //                HRIRdataPoles[d][1] = new float[numTimeSteps]; // ele = 180 pole
//                //                for (int t = 0; t < numTimeSteps; ++t) {
//                //                    HRIRdataPoles[d][1][t] = *((float*) &dataBuffer[index*sizeof(float)]);
//                //                    ++index;
//                //                }
//            }
//            // cleanup the temp array
//            delete[] dataBuffer;
//        } else {
//            // failed to open hrtf binary file, load up zeros
//            HRIRdata = new float****[numDistanceSteps];
//            for (int d = 0; d < numDistanceSteps; ++d) {
//                HRIRdata[d] = new float***[numAzimuthSteps];
//                for (int a = 0; a < numAzimuthSteps; ++a) {
//                    HRIRdata[d][a] = new float**[numElevationSteps];
//                    for (int e = 0; e < numElevationSteps; ++e) {
//                        HRIRdata[d][a][e] = new float*[2];
//                        for (int ch = 0; ch < 2; ++ch) {
//                            HRIRdata[d][a][e][ch] = new float[numTimeSteps];
//                            for (int t = 0; t < numTimeSteps; ++t) {
//                                HRIRdata[d][a][e][ch][t] = 0;
//                            }
//                        }
//                    }
//                }
//            }
//            // pole data is the same for both channels
//            HRIRdataPoles = new float***[numDistanceSteps];
//            for (int d = 0; d < numDistanceSteps; ++d) {
//                HRIRdataPoles[d] = new float**[2];
//                HRIRdataPoles[d][0] = new float*[2]; // ele = 0 pole
//                HRIRdataPoles[d][0][0] = new float[numTimeSteps];
//                HRIRdataPoles[d][0][1] = new float[numTimeSteps];
//                HRIRdataPoles[d][1] = new float*[2]; // ele = 180 pole
//                HRIRdataPoles[d][1][0] = new float[numTimeSteps];
//                HRIRdataPoles[d][1][1] = new float[numTimeSteps];
//                for (int t = 0; t < numTimeSteps; ++t) {
//                    HRIRdataPoles[d][0][0][t] = 0;
//                    HRIRdataPoles[d][0][1][t] = 0;
//                    HRIRdataPoles[d][1][0][t] = 0;
//                    HRIRdataPoles[d][1][1][t] = 0;
//                }
//            }
//        }
//        
//        //        // compact data
//        //        // binary hrtf file name
//        //        String path = File::getSpecialLocation(File::currentApplicationFile).getParentDirectory().getFullPathName();
//        //        path += "/3dAudioDataCompact.bin";
//        //        // basic read (should be cross platform)
//        //        // open the stream
//        //        std::ifstream is(path.getCharPointer());
//        //        if (is.good()) {
//        //            // determine the file length
//        //            is.seekg(0, is.end);
//        //            size_t size = is.tellg();
//        //            is.seekg(0, is.beg);
//        //            // create a temp array to store the data
//        //            char* dataBuffer = new char[size];
//        //            // load the data
//        //            is.read(dataBuffer, size);
//        //            // close the file
//        //            is.close();
//        //            // pack data into HRIR array
//        //            int index = 0;
//        //            HRIRdata = new float****[numDistanceSteps];
//        //            for (int d = 0; d < numDistanceSteps; ++d) {
//        //                HRIRdata[d] = new float***[numAzimuthSteps/2+1];
//        //                for (int a = 0; a < numAzimuthSteps/2+1; ++a) {
//        //                    HRIRdata[d][a] = new float**[numElevationSteps];
//        //                    for (int e = 0; e < numElevationSteps; ++e) {
//        //                        HRIRdata[d][a][e] = new float*[2];
//        //                        for (int ch = 0; ch < 2; ++ch) {
//        //                            HRIRdata[d][a][e][ch] = new float[numTimeSteps];
//        //                            for (int t = 0; t < numTimeSteps; ++t) {
//        //                                HRIRdata[d][a][e][ch][t] = *((float*) &dataBuffer[index*sizeof(float)]);
//        //                                ++index;
//        //                            }
//        //                        }
//        //                    }
//        //                }
//        //            }
//        //            // cleanup the temp array
//        //            delete[] dataBuffer;
//        //        } else {
//        //            // failed to open hrtf binary file, load up zeros
//        //            int index = 0;
//        //            HRIRdata = new float****[numDistanceSteps];
//        //            for (int d = 0; d < numDistanceSteps; ++d) {
//        //                HRIRdata[d] = new float***[numAzimuthSteps/2+1];
//        //                for (int a = 0; a < numAzimuthSteps/2+1; ++a) {
//        //                    HRIRdata[d][a] = new float**[numElevationSteps];
//        //                    for (int e = 0; e < numElevationSteps; ++e) {
//        //                        HRIRdata[d][a][e] = new float*[2];
//        //                        for (int ch = 0; ch < 2; ++ch) {
//        //                            HRIRdata[d][a][e][ch] = new float[numTimeSteps];
//        //                            for (int t = 0; t < numTimeSteps; ++t) {
//        //                                HRIRdata[d][a][e][ch][t] = 0;
//        //                                ++index;
//        //                            }
//        //                        }
//        //                    }
//        //                }
//        //            }
//        //        }
//        
//        //        // non-compact (both azimuth sides) and unnormalized data
//        //        // binary hrtf file name
//        //        //String path = File::getSpecialLocation(File::currentApplicationFile).getFullPathName();
//        //        String path = File::getSpecialLocation(File::currentApplicationFile).getParentDirectory().getFullPathName();
//        //        // probably not necessary...
//        //        //#ifdef _WIN32
//        //        //        path += "\";
//        //        //#else
//        //        //        path += "/";
//        //        //#endif
//        //        path += "/3dAudioData.bin";
//        //        //path += "/Contents/3dAudioData.bin";
//        //        //char* fileName = (char*)"/Users/AndrewBarker/Documents/Programming/3dAudio/SphericalHRIR2.bin";
//        //
//        //        // basic read (should be cross platform)
//        //        // open the stream
//        //        //std::ifstream is(fileName);
//        //        std::ifstream is(path.getCharPointer());
//        //        if (is.good()) {
//        //            // determine the file length
//        //            is.seekg(0, is.end);
//        //            size_t size = is.tellg();
//        //            is.seekg(0, is.beg);
//        //            // create a temp array to store the data
//        //            char* dataBuffer = new char[size];
//        //            // load the data
//        //            is.read(dataBuffer, size);
//        //            // close the file
//        //            is.close();
//        //            // pack data into HRIR array
//        //            int index = 0;
//        //            HRIRdata = new float****[numDistanceSteps];
//        //            for (int d = 0; d < numDistanceSteps; ++d) {
//        //                HRIRdata[d] = new float***[numAzimuthSteps];
//        //                for (int a = 0; a < numAzimuthSteps; ++a) {
//        //                    HRIRdata[d][a] = new float**[numElevationSteps];
//        //                    for (int e = 0; e < numElevationSteps; ++e) {
//        //                        HRIRdata[d][a][e] = new float*[2];
//        //                        for (int ch = 0; ch < 2; ++ch) {
//        //                            HRIRdata[d][a][e][ch] = new float[numTimeSteps];
//        //                            for (int t = 0; t < numTimeSteps; ++t) {
//        //                                HRIRdata[d][a][e][ch][t] = *((float*) &dataBuffer[index*sizeof(float)]);
//        //                                ++index;
//        //                            }
//        //                        }
//        //                    }
//        //                }
//        //            }
//        ////            for (int d = 0; d < numDistanceSteps; ++d) {
//        ////                for (int t = 0; t < numTimeSteps; ++t) {
//        ////                    float avg = 0;
//        ////                    for (int ch = 0; ch < 2; ++ch) {
//        ////                        for (int a = 0; a < numAzimuthSteps; ++a) {
//        ////                            avg += HRIRdata[d][a][0][ch][t];
//        ////                        }
//        ////                    }
//        ////                    avg /= numAzimuthSteps*2;
//        ////                    for (int ch = 0; ch < 2; ++ch) {
//        ////                        for (int a = 0; a < numAzimuthSteps; ++a) {
//        ////                            HRIRdata[d][a][0][ch][t] = avg;
//        ////                        }
//        ////                    }
//        ////                }
//        ////            }
//        //            // cleanup the temp array
//        //            delete[] dataBuffer;
//        //        } else {
//        //            // failed to open hrtf binary file, load up zeros
//        //            int index = 0;
//        //            HRIRdata = new float****[numDistanceSteps];
//        //            for (int d = 0; d < numDistanceSteps; ++d) {
//        //                HRIRdata[d] = new float***[numAzimuthSteps];
//        //                for (int a = 0; a < numAzimuthSteps; ++a) {
//        //                    HRIRdata[d][a] = new float**[numElevationSteps];
//        //                    for (int e = 0; e < numElevationSteps; ++e) {
//        //                        HRIRdata[d][a][e] = new float*[2];
//        //                        for (int ch = 0; ch < 2; ++ch) {
//        //                            HRIRdata[d][a][e][ch] = new float[numTimeSteps];
//        //                            for (int t = 0; t < numTimeSteps; ++t) {
//        //                                HRIRdata[d][a][e][ch][t] = 0;
//        //                                ++index;
//        //                            }
//        //                        }
//        //                    }
//        //                }
//        //            }
//        //        }
//        
//    }
//    
//    ++numRefs;
//    
//    // pre-allocate space for maximum number of playableSources, so we don't have to in processBlock()
//    playableSources.resize(8);
//    
//    //    inited = false;
//    //    /* Open Output File */
//    //    outputfp = fopen("resampleTest.txt", "w");
//    //    if(!outputfp){
//    //        perror("Error Opening Output File");
//    //        //exit(0);
//    //    }
//    //logFile1.open("resampleTest1.txt");
//}
//
//ThreeDAudioProcessor::~ThreeDAudioProcessor()
//{
//    // cleanup memeory for undo's
//    clearUndoHistory();
//    {
//        const ScopedLock lockBefore (beforeUndo.getLock());
//        const ScopedLock lockCurrent (currentUndo.getLock());
//        for (int s = 0; s < beforeUndo.size(); ++s)
//            delete beforeUndo[s];
//        for (int s = 0; s < currentUndo.size(); ++s)
//            delete currentUndo[s];
//    }
//    // cleanup sources
//    {
//        const ScopedLock lockSources (sources.getLock());
//        for (int s = 0; s < sources.size(); ++s)
//            delete sources[s];
//    }
//    // cleanup hrir data if we are closing the only plugin instance
//    if (numRefs == 1) {
//        for (int d = numDistanceSteps-1; d >= 0; --d) {
//            //for (int a = numAzimuthSteps-1; a >= 0; --a) {
//            for (int a = numAzimuthSteps/2; a >= 0; --a) {
//                //for (int e = numElevationSteps-1; e >= 0; --e) {
//                for (int e = numElevationSteps-2; e >= 0; --e) {
//                    for (int ch = 1; ch >= 0; --ch) {
//                        delete[] HRIRdata[d][a][e][ch];
//                    }
//                    delete[] HRIRdata[d][a][e];
//                }
//                delete[] HRIRdata[d][a];
//            }
//            delete[] HRIRdata[d];
//        }
//        delete[] HRIRdata;
//        for (int d = numDistanceSteps-1; d >= 0; --d) {
//            for (int e = 1; e >= 0; --e) {
//                for (int ch = 1; ch >= 0; --ch) {
//                    delete[] HRIRdataPoles[d][e][ch];
//                }
//                delete[] HRIRdataPoles[d][e];
//            }
//            delete[] HRIRdataPoles[d];
//        }
//        delete[] HRIRdataPoles;
//    }
//    --numRefs;
//    /* Close Output File */
//    //    fclose(outputfp);
//    //logFile1.close();
//}
//
//void ThreeDAudioProcessor::addSourceAtXYZ(const float (&xyz)[3])
//{
//    const ScopedLock lockSources (sources.getLock());
//    // get the sources before adding the new source
//    saveCurrentState(-1);
//    // add a new source at this xyz pos
//    sources.add(new SoundSource({xyz[0],xyz[1],xyz[2]}));
//    sources.getLast()->setSourceSelected(true);
//    // new undo/redo transaction
//    saveCurrentState(1);
//}
//
//void ThreeDAudioProcessor::copySelectedSources()
//{
//    const ScopedLock lockSources (sources.getLock());
//    bool doUndoableAction = false;
//    // copy the selected sources
//    int beforeNumSources = sources.size();
//    for (int s = 0; s < beforeNumSources; ++s) {
//        if (sources[s]->getSourceSelected()) {
//            // are there any selected path points ?
//            const std::vector<bool> pathPtsSelected = sources[s]->getSelectedPathPoints();
//            if (std::find(pathPtsSelected.begin(), pathPtsSelected.end(), true) != pathPtsSelected.end()) {
//                // if so copy these inside of their respective source's path points
//                // get the sources before copying
//                if (!doUndoableAction) {
//                    saveCurrentState(1);
//                    saveCurrentState(0);
//                    doUndoableAction = true;
//                }
//                sources[s]->copySelectedPathPoints();
//            } else {
//                // if no path points are selected, then copy the whole source
//                // deselect the source we are copying
//                sources[s]->setSourceSelected(false);
//                if (sources.size() < 8) {
//                    // get the sources before copying
//                    if (!doUndoableAction) {
//                        saveCurrentState(1);
//                        saveCurrentState(0);
//                        doUndoableAction = true;
//                    }
//                    // add a new source that is an exact copy of the sourceToCopy
//                    {
//                        const ScopedLock lockBefore (beforeUndo.getLock());
//                        sources.add(new SoundSource(*beforeUndo[s]));
//                    }
//                    // select the new copy of the source and all its path control points
//                    sources.getLast()->setSourceSelected(true);
//                    sources.getLast()->setAllPathPointsSelected(true);
//                }
//            }
//        } // crash when deallocating pathPtsSelected array...
//    }
//}
//
//// saves the current sources state beforeOrAfter == -1 -> before edit w/ reset,
////                                 beforeOrAfter == 0 -> before edit,
////                                 beforeOrAfter == 1 -> after edit
//void ThreeDAudioProcessor::saveCurrentState(const int beforeOrAfter)
//{
//    const ScopedLock lock (sources.getLock());
//    const ScopedLock lockBefore (beforeUndo.getLock());
//    switch (beforeOrAfter) {
//        case -1:
//            // reset global before state
//            for (int s = 0; s < beforeUndo.size(); ++s)
//                delete beforeUndo[s];
//            beforeUndo.clear();
//            // save the sources before edit state
//            for (int s = 0; s < sources.size(); ++s)
//                beforeUndo.add(new SoundSource(*sources[s]));
//            break;
//        case 0:
//            // if before edit snapshot of sources is empty
//            if (beforeUndo.size() == 0) {
//                // save the sources before edit state
//                for (int s = 0; s < sources.size(); ++s)
//                    beforeUndo.add(new SoundSource(*sources[s]));
//            }
//            break;
//        case 1:
//            // if we have the source state before edit available
//            if (beforeUndo.size() > 0) {
//                //bool doUndo = true;
//                //                if (beforeUndo.size() == sources.size())
//                //                    for (int s = 0; s < sources.size(); ++s)
//                //                        if ((*beforeUndo[s]).isSameAs(*sources[s]))
//                //                            doUndo = false;
//                //if (doUndo) {
//                const ScopedLock lockCurrent (currentUndo.getLock());
//                // new undo/redo transaction
//                beginNewTransaction();
//                // make sure current is cleared
//                for (int s = 0; s < currentUndo.size(); ++s)
//                    delete currentUndo[s];
//                currentUndo.clear();
//                // get the current sources states
//                for (int s = 0; s < sources.size(); ++s)
//                    currentUndo.add(new SoundSource(*sources[s]));
//                // store the sources states of before and after
//                perform(new EditSources(beforeUndo, currentUndo, this));
//                // cleanup
//                for (int s = 0; s < beforeUndo.size(); ++s)
//                    delete beforeUndo[s];
//                beforeUndo.clear();
//                for (int s = 0; s < currentUndo.size(); ++s)
//                    delete currentUndo[s];
//                currentUndo.clear();
//                //}
//            }
//            break;
//    }
//}
//
//void ThreeDAudioProcessor::getSourcePosXYZ(const int sourceIndex, float (&xyz)[3]) const
//{
//    const ScopedLock lockSources (sources.getLock());
//    std::array<float,3> pos = sources[sourceIndex]->getPosXYZ();
//    xyz[0] = pos[0];
//    xyz[1] = pos[1];
//    xyz[2] = pos[2];
//}
//
//void ThreeDAudioProcessor::setSourceSelected(const int sourceIndex, const bool newSelectedState)
//{
//    const ScopedLock lockSources (sources.getLock());
//    // set source's selected state
//    sources[sourceIndex]->setSourceSelected(newSelectedState);
//    // if deselecting
//    if (!newSelectedState) {
//        // deselect all of source's path points as well
//        for (int j = 0; j < sources[sourceIndex]->getNumPathPoints(); ++j)
//            sources[sourceIndex]->setPathPointSelected(j, false);
//        // make a state snapshot for undo/redos
//        saveCurrentState(1);
//    }
//}
//
////// for use when switching to the automation view so we can be sure to see any existing sources, don't want any undo behavior here
////void ThreeDAudioProcessor::selectAllSourcesIfNoneSelected()
////{
////    const ScopedLock lockSources (sources.getLock());
////    // see if any sources are selected
////    bool noSourcesSelected = true;
////    for (int s = 0; s < sources.size(); ++s) {
////        if (sources[s]->getSourceSelected()) {
////            noSourcesSelected = false;
////            break;
////        }
////    }
////    // if the user didn't select any explicitly, just automatically select all the sources for automating
////    if (noSourcesSelected) {
////        for (int s = 0; s < sources.size(); ++s)
////            sources[s]->setSourceSelected(true);
////    }
////}
//
//// this function is intended to provide functionality for select all(control/cmd + a) and a click in space to deselect all
//void ThreeDAudioProcessor::selectAllSources(const bool newSelectedState)
//{
//    const ScopedLock lockSources (sources.getLock());
//    // if selecting
//    if (newSelectedState) {
//        for (int s = 0; s < sources.size(); ++s) {
//            // if a source is already selected and we are the newSelectedState = true, select its path points
//            if (sources[s]->getSourceSelected()) {
//                for (int j = 0; j < sources[s]->getNumPathPoints(); ++j) {
//                    sources[s]->setPathPointSelected(j, true);
//                }
//            } else {
//                // otherwise just select the unselected source
//                sources[s]->setSourceSelected(newSelectedState);
//            }
//        }
//    }
//    // if deselecting
//    else {
//        // unselect all sources and their potentially selected path points
//        for (int s = 0; s < sources.size(); ++s) {
//            sources[s]->setSourceSelected(newSelectedState);
//            for (int j = 0; j < sources[s]->getNumPathPoints(); ++j) {
//                sources[s]->setPathPointSelected(j, false);
//            }
//        }
//        // make a state snapshot for undo/redos
//        saveCurrentState(1);
//    }
//}
//
//bool ThreeDAudioProcessor::getSourceSelected(const int sourceIndex)
//{
//    const ScopedLock lockSources (sources.getLock());
//    return sources[sourceIndex]->getSourceSelected();
//}
//
//void ThreeDAudioProcessor::deleteSelectedSources()
//{
//    const ScopedLock lockSources (sources.getLock());
//    bool doUndoableAction = false;
//    // array to tell caller (gl) which source indicies it actually deleted
//    //Array<int> deletedSourceIndicies;
//    int k = 0;
//    for (int i = 0; i < sources.size(); ++i) {
//        if (sources[i]->getSourceSelected()) {
//            if (!doUndoableAction) {
//                saveCurrentState(-1);
//                doUndoableAction = true;
//            }
//            const int numPtsDeleted = sources[i]->deleteSelectedPathPoints();
//            // no remaining path segments exist so we need to set the moving state back to sationary (otherwise problems used to occur)
//            //if (sources[i]->getNumPathPoints() <= 1)
//            //sources[i]->setSourceMoving(false);
//            // no path points or all points were selected and deleted so delete the source itself
//            if (numPtsDeleted == 0 || sources[i]->getNumPathPoints() == 0) {
//                delete sources[i]; // newly added with interp stuff
//                sources.remove(i);
//                //deletedSourceIndicies.add(k);
//                --i;
//            }
//        }
//        ++k;
//    }
//    if (doUndoableAction)
//        saveCurrentState(1);
//    //return deletedSourceIndicies;
//}
//
//void ThreeDAudioProcessor::toggleLockSourcesToPaths()
//{
//    lockSourcesToPaths = !lockSourcesToPaths;
//    if (!lockSourcesToPaths)
//    {   // make sure to unmute any muted sources if unlocking
//        const ScopedLock lockSources (sources.getLock());
//        for (int s = 0; s < sources.size(); ++s)
//            sources[s]->setSourceMuted(false);
//    }
//}
//
//bool ThreeDAudioProcessor::getLockSourcesToPaths() const
//{
//    return lockSourcesToPaths;
//}
//
//void ThreeDAudioProcessor::toggleDoppler()
//{
//    dopplerOn = !dopplerOn;
//    // TODO: detect largest latency of dopplers for each source and adjust (along with resampling) with setLatencySamples()
//}
//
//int ThreeDAudioProcessor::moveSelectedSourcesXYZ(const float dx, const float dy, const float dz)
//{
//    const ScopedLock lockSources (sources.getLock());
//    int movedStuff = 0;
//    for (int i = 0; i < sources.size(); ++i) {
//        // in this case we want to move the source
//        if (sources[i]->getSourceSelected()) {
//            // get the sources before first move
//            saveCurrentState(0);
//            // if any of the selected source's path points are selected, move only them
//            if (!sources[i]->moveSelectedPathPointsXYZ(dx, dy, dz)) {
//                // don't move sources if we are playing and sources are locked to paths
//                if (!(playing && lockSourcesToPaths && sources[i]->getNumPathPoints() > 1)) {
//                    std::array<float,3> pos = sources[i]->getPosXYZ();
//                    pos[0] += dx;
//                    pos[1] += dy;
//                    pos[2] += dz;
//                    // bounds checking should be done by this func
//                    sources[i]->setPosXYZ(pos);
//                }
//            }
//            movedStuff = 1;
//        }
//    }
//    return movedStuff;
//}
//
//int ThreeDAudioProcessor::moveSelectedSourcesRAE(const float dRad, const float dAzi, const float dEle)
//{
//    const ScopedLock lockSources (sources.getLock());
//    int movedStuff = 0;
//    for (int i = 0; i < sources.size(); ++i) {
//        // in this case we want to move the source
//        if (sources[i]->getSourceSelected()) {
//            // get sources before first move
//            saveCurrentState(0);
//            // if any of the selected source's path points are selected, move only them
//            if (!sources[i]->moveSelectedPathPointsRAE(dRad, dAzi, dEle)) {
//                // don't move sources if we are playing and sources are locked to valid paths (with more than 1 pt)
//                if (!(playing && lockSourcesToPaths && sources[i]->getNumPathPoints() > 1)) {
//                    // otherwise move the selected sources
//                    std::array<float,3> pos = sources[i]->getPosRAE();
//                    pos[0] *= dRad;
//                    pos[1] += dAzi;
//                    pos[2] += sources[i]->getEleDir()*dEle;
//                    // bounds checking should be done by this func
//                    sources[i]->setPosRAE(pos);
//                }
//            }
//            movedStuff = 1;
//        }
//    }
//    return movedStuff;
//}
//
//void ThreeDAudioProcessor::dropPathPoint()
//{
//    const ScopedLock lockSources (sources.getLock());
//    bool doUndoableAction = false;
//    // drop a new path point for each selected source at the current position
//    for (int i = 0; i < sources.size(); ++i) {
//        if (sources[i]->getSourceSelected()) {
//            if (!doUndoableAction) {
//                saveCurrentState(-1);
//                doUndoableAction = true;
//            }
//            sources[i]->addPathPoint();
//        }
//    }
//    if (doUndoableAction)
//        saveCurrentState(1);
//}
//
//bool ThreeDAudioProcessor::dropPathPoint(const float (&xyz)[3])
//{
//    const ScopedLock lockSources (sources.getLock());
//    bool doUndoableAction = false;
//    bool sourcesSelected = false;
//    // drop a new path point for each selected source at the specified position
//    for (int i = 0; i < sources.size(); i++) {
//        if (sources[i]->getSourceSelected()) {
//            if (!doUndoableAction) {
//                saveCurrentState(-1);
//                doUndoableAction = true;
//            }
//            std::array<float, 3> pos = {xyz[0],xyz[1],xyz[2]};
//            sources[i]->addPathPoint(pos);
//            sourcesSelected = true;
//        }
//    }
//    if (doUndoableAction)
//        saveCurrentState(1);
//    return sourcesSelected;
//}
//
//void ThreeDAudioProcessor::togglePathPointSelected(const int sourceIndex, const int ptIndex)
//{
//    const ScopedLock lockSources (sources.getLock());
//    bool nextState = ! sources[sourceIndex]->getPathPointSelected(ptIndex);
//    sources[sourceIndex]->setPathPointSelected(ptIndex, nextState);
//    // if deselecting make a state snapshot for undo/redos
//    if (!nextState)
//        saveCurrentState(1);
//}
//
//void ThreeDAudioProcessor::setPathPointSelectedState(const int sourceIndex, const int ptIndex,
//                                                     const bool newSelectedState)
//{
//    const ScopedLock lockSources (sources.getLock());
//    sources[sourceIndex]->setPathPointSelected(ptIndex, newSelectedState);
//    // if deselecting make a state snapshot for undo/redos
//    if (!newSelectedState)
//        saveCurrentState(1);
//}
//
//void ThreeDAudioProcessor::selectAllPathAutomationView(const bool newSelectedState)
//{
//    const ScopedLock lockSources (sources.getLock());
//    for (int s = 0; s < sources.size(); ++s) {
//        if (sources[s]->getSourceSelected())
//            sources[s]->setAllPathAutomationPointsSelected(newSelectedState);
//    }
//}
//
//void ThreeDAudioProcessor::setPathAutomationPointSelectedState(const int sourceIndex, const int ptIndex,
//                                                               const bool newSelectedState)
//{
//    const ScopedLock lockSources (sources.getLock());
//    sources[sourceIndex]->setPathAutomationPointSelected(ptIndex, newSelectedState);
//    // if deselecting make a state snapshot for undo/redos
//    if (!newSelectedState)
//        saveCurrentState(1);
//}
//
//void ThreeDAudioProcessor::togglePathAutomationPointSelected(const int sourceIndex, const int ptIndex)
//{
//    const ScopedLock lockSources (sources.getLock());
//    bool nextState = ! sources[sourceIndex]->getPathAutomationPointSelected(ptIndex);
//    sources[sourceIndex]->setPathAutomationPointSelected(ptIndex, nextState);
//    // if deselecting make a state snapshot for undo/redos
//    if (!nextState)
//        saveCurrentState(1);
//}
//
//void ThreeDAudioProcessor::deselectAllPathAutomationPoints()
//{
//    const ScopedLock lockSources (sources.getLock());
//    for (int s = 0; s < sources.size(); ++s)
//        sources[s]->setAllPathAutomationPointsSelected(false);
//    // make a state snapshot for undo/redos
//    saveCurrentState(1);
//}
//
//// return value: = 0 no pts selected/moved, > 0 pts moved as desired, < 0 pts maybe moved but not completely as desired due to bounds constraints applied to the entire selected group
//int ThreeDAudioProcessor::moveSelectedPathAutomationPoints(float dx, float dy)
//{
//    const ScopedLock lockSources (sources.getLock());
//    // if we can't make the full position change due to bounds constraints on the entire selected group of points, then sign goes negative
//    int sign = 1;
//    // bounds check dx and dy so all selected path auto pts are moved as a group and all stay within bounds
//    for (int s = 0; s < sources.size(); ++s) {
//        // if source is selected (its path auto pts are visible on path auto screen)
//        if (sources[s]->getSourceSelected()) {
//            // get sources before first move
//            saveCurrentState(0);
//            std::vector<std::vector<float>> pts = sources[s]->getPathPosPtr()->getSelectedPoints();
//            float newX, newY;
//            for (int i = 0; i < pts.size(); ++i) {
//                newX = pts[i][0] + dx;
//                newY = pts[i][1] + dy;
//                if (newX < 0) {
//                    dx -= newX;
//                    sign = -1;
//                }
//                if (newY < 0) {
//                    dy -= newY;
//                    sign = -1;
//                }
//                if (newY > 1) {
//                    dy -= newY-1;
//                    sign = -1;
//                }
//            }
//        }
//    }
//    int numMoved = 0;
//    for (int s = 0; s < sources.size(); ++s) {
//        // the source must be selected as well to adjust its path automation points
//        if (sources[s]->getSourceSelected())
//            numMoved += sources[s]->moveSelectedPathAutomationPoints(dx, dy);
//    }
//    return sign*numMoved;
//}
//
////std::vector<int> ThreeDAudioProcessor::moveSelectedPathAutomationPointsWithReorderingInfo(float dx, float dy, const int sourceIndexOfInterest)
////{
////    const ScopedLock lockSources (sources.getLock());
////    // if we can't make the full position change due to bounds constraints on the entire selected group of points, then sign goes negative
////    //int sign = 1;
////    // bounds check dx and dy so all selected path auto pts are moved as a group and all stay within bounds
////    for (int s = 0; s < sources.size(); ++s) {
////        // if source is selected (its path auto pts are visible on path auto screen)
////        if (sources[s]->getSourceSelected()) {
////            // get sources before first move
////            saveCurrentState(0);
////            std::vector<std::vector<float>> pts = sources[s]->getPathPosPtr()->getSelectedPoints();
////            float newX, newY;
////            for (int i = 0; i < pts.size(); ++i) {
////                newX = pts[i][0] + dx;
////                newY = pts[i][1] + dy;
////                if (newX < 0) {
////                    dx -= newX;
////                    //sign = -1;
////                }
////                if (newY < 0) {
////                    dy -= newY;
////                    //sign = -1;
////                }
////                if (newY > 1) {
////                    dy -= newY-1;
////                    //sign = -1;
////                }
////            }
////        }
////    }
////    //int numMoved = 0;
////    std::vector<int> reorderingInfo;
////    for (int s = 0; s < sources.size(); ++s) {
////        // the source must be selected as well to adjust its path automation points
////        if (sources[s]->getSourceSelected())
////            if (s == sourceIndexOfInterest)
////                reorderingInfo = sources[s]->moveSelectedPathAutomationPointsWithReorderingInfo(dx, dy);
////            else
////                sources[s]->moveSelectedPathAutomationPoints(dx, dy);
////    }
////    return reorderingInfo;
////}
//
//void ThreeDAudioProcessor::moveSelectedPathAutomationPointsTo(const int referencePtSourceIndex,
//                                                              int& referencePtIndex,
//                                                              const int referencePtIndexAmongSelecteds,
//                                                              const float x, const float y)
//{
//    const ScopedLock lockSources (sources.getLock());
//    const std::vector<float> refPt = std::get<0>(sources[referencePtSourceIndex]->getPathPosPtr()
//                                                 ->getSelectedPoint(referencePtIndexAmongSelecteds));//getPoint(referencePtIndex);
//    moveSelectedPathAutomationPoints(x - refPt[0], y - refPt[1]);
//    referencePtIndex = std::get<1>(sources[referencePtSourceIndex]->getPathPosPtr()
//                                   ->getSelectedPoint(referencePtIndexAmongSelecteds));//std::get<1>(refPt);
//    ////    std::vector<int> reorderingInfo;
//    ////    reorderingInfo = moveSelectedPathAutomationPointsWithReorderingInfo(x - refPt[0], y - refPt[1], referencePtSourceIndex);
//    //    if (moveSelectedPathAutomationPoints(x - refPt[0], y - refPt[1]))
//    //    //if (reorderingInfo.size() > 0)
//    //    {
//    //        const std::vector<std::tuple<std::vector<float>, int>> movedPts = sources[referencePtSourceIndex]->getPathPosPtr()->getSelectedPointsWithIndecies();
//    //        std::vector<float> newRefPt {x, y};
//    //        bool found = false;
//    //        for (auto pt : movedPts)
//    //        {
//    //            if (std::get<0>(pt) == newRefPt) {
//    //            //if (std::get<0>(pt)[0] == x && std::get<1>(pt) == )
//    //                referencePtIndex = std::get<1>(pt);
//    //                found = true;
//    //            }
//    //        }
//    //        if (!found)
//    //            bool bummer = true;
//    ////        referencePtIndex = std::distance(reorderingInfo.begin(),
//    ////                                         std::find(reorderingInfo.begin(), reorderingInfo.end(), referencePtIndex));
//    ////        // need to catch this case because the reordered point indicies don't reflect i
//    ////        const int numPts = sources[referencePtSourceIndex]->getPathPosPtr()->getNumPoints();
//    ////        if (referencePtIndex == numPts - 2
//    ////            && sources[referencePtSourceIndex]->getPathPosPtr()->getPoint(referencePtIndex)[0]
//    ////               < sources[referencePtSourceIndex]->getPathPosPtr()->getPoint(numPts-1)[0])
//    ////        {
//    ////            referencePtIndex = numPts - 1;
//    ////        }
//    //    }
//}
//
//void ThreeDAudioProcessor::addPathAutomationPtAtXY(const float (&xy)[2])
//{
//    const ScopedLock lockSources (sources.getLock());
//    bool doUndoableAction = false;
//    for (int i = 0; i < sources.size(); ++i) {
//        if (sources[i]->getSourceSelected()) {
//            if (!doUndoableAction) {
//                saveCurrentState(-1);
//                doUndoableAction = true;
//            }
//            sources[i]->addPathAutomationPoint(xy[0], xy[1]);
//        }
//    }
//    if (doUndoableAction) {
//        saveCurrentState(1);
//    }
//}
//
//void ThreeDAudioProcessor::deleteSelectedAutomationPoints()
//{
//    const ScopedLock lockSources (sources.getLock());
//    //    // get the sources before edit
//    //    Array<SoundSource*, CriticalSection> before;
//    bool doUndoableAction = false;
//    //    for (int s = 0; s < sources.size(); ++s)
//    //        before.add(new SoundSource(*sources[s]));
//    for (int i = 0; i < sources.size(); ++i) {
//        if (sources[i]->getPathPosPtr()->getNumSelectedPoints()) {
//            saveCurrentState(-1);
//            doUndoableAction = true;
//        }
//        sources[i]->deleteSelectedAutomationPoints();
//        //        if (sources[i]->deleteSelectedAutomationPoints()) {
//        //            saveCurrentState(-1);
//        //            doUndoableAction = true;
//        //        }
//    }
//    if (doUndoableAction)
//        saveCurrentState(1);
//    //    if (doUndoableAction) {
//    //        // store the sources states of before and after
//    //        beginNewTransaction();
//    //        Array<SoundSource*, CriticalSection> current;
//    //        for (int s = 0; s < sources.size(); ++s)
//    //            current.add(new SoundSource(*sources[s]));
//    //        perform(new EditSources(before, current, this));
//    //        // cleanup
//    //        for (int s = 0; s < current.size(); ++s)
//    //            delete current[s];
//    //        // reset global before state
//    //        const ScopedLock lockBefore (beforeUndo.getLock());
//    //        for (int s = 0; s < beforeUndo.size(); ++s)
//    //            delete beforeUndo[s];
//    //        beforeUndo.clear();
//    //    }
//    //    // cleanup
//    //    for (int s = 0; s < before.size(); ++s)
//    //        delete before[s];
//}
//
//void ThreeDAudioProcessor::setSelectedPathAutomationPointsSegmentType(const int newSegType)
//{
//    const ScopedLock lockSources (sources.getLock());
//    // get the sources before edit
//    Array<SoundSource*, CriticalSection> before;
//    bool doUndoableAction = false;
//    for (int s = 0; s < sources.size(); ++s)
//        before.add(new SoundSource(*sources[s]));
//    for (int i = 0; i < sources.size(); ++i) {
//        //        if (sources[i]->getPathPosPtr()->getSelectedSplines().size()) {
//        //            saveCurrentState(-1);
//        //            doUndoableAction = true;
//        //        }
//        //        sources[i]->setSelectedPathAutomationPointsSegmentType(newSegType);
//        if (sources[i]->setSelectedPathAutomationPointsSegmentType(newSegType)) {
//            doUndoableAction = true;
//        }
//    }
//    //    if (doUndoableAction)
//    //        saveCurrentState(1);
//    if (doUndoableAction)
//    {   // store the sources states of before and after
//        beginNewTransaction();
//        Array<SoundSource*, CriticalSection> current;
//        for (int s = 0; s < sources.size(); ++s)
//            current.add(new SoundSource(*sources[s]));
//        perform(new EditSources(before, current, this));
//        // cleanup
//        for (int s = 0; s < current.size(); ++s)
//            delete current[s];
//        // reset global before state
//        const ScopedLock lockBefore (beforeUndo.getLock());
//        for (int s = 0; s < beforeUndo.size(); ++s)
//            delete beforeUndo[s];
//        beforeUndo.clear();
//    }
//    // cleanup
//    for (int s = 0; s < before.size(); ++s)
//        delete before[s];
//}
//
//void ThreeDAudioProcessor::toggleSelectedSourcesPathType()
//{
//    const ScopedLock lockSources (sources.getLock());
//    saveCurrentState(-1);
//    for (int i = 0; i < sources.size(); ++i) {
//        if (sources[i]->getSourceSelected() && sources[i]->getPath().get() != nullptr) {
//            int pathType = sources[i]->getPath()->type;
//            pathType += 1;
//            if (pathType > 1)
//                pathType -= 2;
//            sources[i]->setPathType(pathType);
//        }
//    }
//    saveCurrentState(1);
//}
//
//std::vector<std::vector<float>> ThreeDAudioProcessor::getPathPoints(const int sourceIndex) const
//{
//    const ScopedLock lockSources (sources.getLock());
//    return sources[sourceIndex]->getPathPoints();
//}
//
//std::vector<bool> ThreeDAudioProcessor::getPathPointsSelected(const int sourceIndex) const
//{
//    const ScopedLock lockSources (sources.getLock());
//    return sources[sourceIndex]->getPath()->getPointsSelected();
//}
//
//void ThreeDAudioProcessor::markPathAsUpdated(const int sourceIndex)
//{
//    const ScopedLock lockSources (sources.getLock());
//    sources[sourceIndex]->setPathChangedState(false);
//}
//
//void ThreeDAudioProcessor::markPathPosAsUpdated(const int sourceIndex)
//{
//    const ScopedLock lockSources (sources.getLock());
//    sources[sourceIndex]->setPathPosChangedState(false);
//}
//
//void ThreeDAudioProcessor::copySelectedPathAutomationPoints()
//{
//    const ScopedLock lockSources (sources.getLock());
//    for (int s = 0; s < sources.size(); ++s)
//    {
//        if (sources[s]->getSourceSelected())
//        {
//            if (sources[s]->getPathPosPtr()->getPointsSelected().size() > 0)
//            {
//                saveCurrentState(1);
//                saveCurrentState(0);
//            }
//            sources[s]->copySelectedPathAutomationPoints();
//        }
//    }
//}
//
//std::vector<std::vector<float>> ThreeDAudioProcessor::getPathAutomationPoints(const int sourceIndex)
//{
//    const ScopedLock lockSources (sources.getLock());
//    return sources[sourceIndex]->getPathPosPtr()->getPoints();
//}
//
////std::vector<std::vector<float>> ThreeDAudioProcessor::getSelectedPathAutomationPoints(const int sourceIndex)
////{
////    const ScopedLock lockSources (sources.getLock());
////    return sources[sourceIndex]->getPathPosPtr()->getSelectedPoints();
////}
//
////std::vector<bool> ThreeDAudioProcessor::getPathAutomationPointsSelected(const int sourceIndex)
////{
////    const ScopedLock lockSources (sources.getLock());
////    return sources[sourceIndex]->getPathPosPtr()->getPointsSelected();
////}
//
//int ThreeDAudioProcessor::getPathAutomationPointIndexAmongSelectedPoints(const int sourceIndex,
//                                                                         const int pointIndex)
//{
//    const std::vector<float> thePoint = sources[sourceIndex]->getPathPosPtr()->getPoint(pointIndex);
//    const std::vector<std::vector<float>> selectedPoints =
//    sources[sourceIndex]->getPathPosPtr()->getSelectedPoints();
//    int i = 0;
//    for (auto pt : selectedPoints)
//    {
//        if (pt == thePoint)
//            return i;
//        ++i;
//    }
//    //    const std::vector<std::tuple<std::vector<float>, int>> selectedPoints =
//    //    sources[sourceIndex]->getPathPosPtr()->getSelectedPointsWithIndecies();
//    //    for (auto pt : selectedPoints)
//    //        if (std::get<0>(pt) == thePoint)
//    //            return std::get<1>(pt);
//    return -1; // failed to find the relative index
//}
//
//bool ThreeDAudioProcessor::areAnySelectedSourcesPathAutomationPointsSelected()
//{
//    const ScopedLock lockSources (sources.getLock());
//    for (auto source : sources) {
//        if (source->getSourceSelected()) {
//            std::vector<bool> pointsSelected = source->getPathPosPtr()->getPointsSelected();
//            if (std::find(pointsSelected.begin(), pointsSelected.end(), true) != pointsSelected.end()) {
//                return true;
//            }
//        }
//    }
//    return false;
//}
//
//void ThreeDAudioProcessor::makeSourcesVisibleForPathAutomationView() noexcept
//{
//    const ScopedLock lockSources (sources.getLock());
//    // see if any sources are selected
//    bool noSourcesSelected = true;
//    for (const auto& s : sources) {
//        if (s->getSourceSelected()) {
//            noSourcesSelected = false;
//            break;
//        }
//    }
//    // if the user didn't select any explicitly, just automatically select all the sources for automating
//    if (noSourcesSelected) {
//        for (auto& s : sources) {
//            s->setSourceSelected(true);
//        }
//    }
//}
//
//// defines a time region to loop over based on the first and last selected path auto points for all selected sources.  if less than two pts are selected, turn off looping
//void ThreeDAudioProcessor::defineLoopingRegionUsingSelectedPathAutomationPoints()
//{
//    const ScopedLock lockSources (sources.getLock());
//    float begin = -1, end = -1, firstSelectedXPos, lastSelectedXPos;
//    int numSelectedPoints = 0;
//    FunctionalInterpolator<float>* pathPos;
//    for (auto source : sources) {
//        if (source->getSourceSelected()) {
//            pathPos = source->getPathPosPtr();
//            numSelectedPoints = pathPos->getNumSelectedPoints();
//            if (numSelectedPoints > 0) {
//                firstSelectedXPos = std::get<0>(pathPos->getSelectedPoint(0))[0];
//                if (begin == -1 || firstSelectedXPos < begin)
//                    begin = firstSelectedXPos;
//                if (end == -1 || firstSelectedXPos > end)
//                    end = firstSelectedXPos;
//            }
//            if (numSelectedPoints > 1) {
//                lastSelectedXPos = std::get<0>(pathPos->getSelectedPoint(numSelectedPoints-1))[0];
//                if (begin == -1 || lastSelectedXPos < begin)
//                    begin = lastSelectedXPos;
//                if (end == -1 || lastSelectedXPos > end)
//                    end = lastSelectedXPos;
//            }
//        }
//    }
//    if (begin != -1 && end != -1 && begin != end) {
//        // if the looping region is the exact same as it was toggle the looping off by reseting back to -1's
//        if (begin == loopRegionBegin && end == loopRegionEnd) {
//            loopRegionBegin = -1;
//            loopRegionEnd = -1;
//        } else { // otherwise set the new region as desired
//            loopRegionBegin = begin;
//            loopRegionEnd = end;
//        }
//    } else {
//        loopRegionBegin = -1;
//        loopRegionEnd = -1;
//    }
//}
//
////// copy constructors get expensive when they are needed once per gl frame and the sources have lots of points in their interps
////Array<SoundSource> ThreeDAudioProcessor::getSourcesCopy()
////{
////    Array<SoundSource> copy;
////    for (int s = 0; s < sources.size(); s++)
////        copy.add(*sources[s]);
////    return copy;
////}
//
////Array<SoundSource*, CriticalSection> ThreeDAudioProcessor::getSources()
////{
////    return sources;
////}
//
//Array<SoundSource*, CriticalSection>* ThreeDAudioProcessor::getSourcesPtr()
//{
//    return &sources;
//}
//
//void ThreeDAudioProcessor::setSources(const Array<SoundSource*, CriticalSection>& newSources)
//{
//    // update sources and force a call to the SoundSource copy construtor
//    const ScopedLock lock1 (sources.getLock());
//    const ScopedLock lock2 (newSources.getLock());
//    for (int s = 0; s < sources.size(); ++s)
//        delete sources[s];
//    sources.clear();
//    for (int s = 0; s < newSources.size(); ++s) {
//        sources.add(new SoundSource(*newSources[s]));
//        sources[s]->setPathChangedState(true);
//        sources[s]->setPathPosChangedState(true);
//    }
//}
//
//void ThreeDAudioProcessor::setSpeedOfSound(const float newSpeedOfSound)
//{
//    speedOfSound = newSpeedOfSound;
//}
//
//void ThreeDAudioProcessor::setProcessingMode(const ProcessingMode newMode) noexcept
//{
//    processingMode = newMode;
//    if (processingMode == ProcessingMode::AUTO_DETECT)
//        realTime.store(isHostRealTime);
//        else
//            realTime = (processingMode == ProcessingMode::REALTIME);
//            }
//
//// JUCE auto-generated stuff
////==============================================================================
//const String ThreeDAudioProcessor::getName() const
//{
//    return JucePlugin_Name;
//}
//
//int ThreeDAudioProcessor::getNumParameters()
//{
//    return 0;
//}
//
//float ThreeDAudioProcessor::getParameter (int index)
//{
//    return 0.0f;
//}
//
//void ThreeDAudioProcessor::setParameter (int index, float newValue)
//{
//}
//
//const String ThreeDAudioProcessor::getParameterName (int index)
//{
//    return String::empty;
//}
//
//const String ThreeDAudioProcessor::getParameterText (int index)
//{
//    return String::empty;
//}
//
//const String ThreeDAudioProcessor::getInputChannelName (int channelIndex) const
//{
//    return String (channelIndex + 1);
//}
//
//const String ThreeDAudioProcessor::getOutputChannelName (int channelIndex) const
//{
//    return String (channelIndex + 1);
//}
//
//bool ThreeDAudioProcessor::isInputChannelStereoPair (int index) const
//{
//    return true;
//}
//
//bool ThreeDAudioProcessor::isOutputChannelStereoPair (int index) const
//{
//    return true;
//}
//
//bool ThreeDAudioProcessor::acceptsMidi() const
//{
//#if JucePlugin_WantsMidiInput
//    return true;
//#else
//    return false;
//#endif
//}
//
//bool ThreeDAudioProcessor::producesMidi() const
//{
//#if JucePlugin_ProducesMidiOutput
//    return true;
//#else
//    return false;
//#endif
//}
//
//bool ThreeDAudioProcessor::silenceInProducesSilenceOut() const
//{
//    return false;
//}
//
//double ThreeDAudioProcessor::getTailLengthSeconds() const
//{
//    // not sure if the tail length reported here should include latency due to resampling
//    if (fs != sampleRate_HRTF) {
//        return (numTimeSteps+1.0) / getSampleRate(); //(2.0*getBlockSize() + numTimeSteps) / getSampleRate();
//    } else {
//        return numTimeSteps / getSampleRate();
//    }
//}
//
//int ThreeDAudioProcessor::getNumPrograms()
//{
//    return 1;
//}
//
//int ThreeDAudioProcessor::getCurrentProgram()
//{
//    return 0;
//}
//
//void ThreeDAudioProcessor::setCurrentProgram (int index)
//{
//}
//
//const String ThreeDAudioProcessor::getProgramName (int index)
//{
//    return String::empty;
//}
//
//void ThreeDAudioProcessor::changeProgramName (int index, const String& newName)
//{
//}
//
//void ThreeDAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
//{
//    // Use this method as the place to do any pre-playback
//    // initialisation that you need..
//    // need this lock to synchronize the updates to the resamplers and playableSources, i think this might not actually be necessary ...
//    //std::lock_guard<std::mutex> lockInitLock (initLock);
//    //initLock.lock();
//    N = samplesPerBlock;
//    if (fs != sampleRate) {
//        fs = sampleRate;
//        // set doppler(s) to the new sample rate, reallocation for this change happens in allocateForMaxBufferSize() below
//        for (auto& s : playableSources)
//            s.setDopplerSampleRate(fs);
//        // TODO: detect largest latency of doppler and factor that in to the setLatencySamples() call below
//    }
//    int playableSourceMaxBufferSize = N;
//    if (fs != sampleRate_HRTF) {
//        resampler = Resampler(fs, N, sampleRate_HRTF, true);
//        unsamplerCh1 = Resampler(sampleRate_HRTF, N, fs, false);
//        unsamplerCh2 = Resampler(sampleRate_HRTF, N, fs, false);
//        setLatencySamples(1);
//        playableSourceMaxBufferSize = resampler.getNoutMax();
//    }
//    if (sources.size() == 0) {
//        // add a source to get started
//        const ScopedLock lockSources (sources.getLock());
//        sources.add(new SoundSource({1, 0, 0}));
//    }
//    // update all the sources' real time state with the processor's
//    isHostRealTime = !isNonRealtime();
//    //    if (editor)
//    //        editor->setProcessingModeAutoDetect(isHostRealTime); // update the auto-detect state for the GUI radio option
//    realTime = (processingMode == ProcessingMode::AUTO_DETECT) ? isHostRealTime.load() : processingMode == ProcessingMode::REALTIME;
//    //    const bool isRealTime = (processingMode == ProcessingMode::AUTO_DETECT) ? !isNonRealtime() : processingMode == ProcessingMode::REALTIME;
//    for (auto& s : playableSources) {
//        //s.setRealTime(isRealTime);
//        s.allocateForMaxBufferSize(playableSourceMaxBufferSize);
//    }
//    // now we are setup for processing
//    inited = true;
//    //initLock.unlock();
//}
//
//void ThreeDAudioProcessor::releaseResources()
//{
//    // When playback stops, you can use this as an opportunity to free up any
//    // spare memory, etc.
//}
//
//void ThreeDAudioProcessor::processBlock (AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
//{
//    // In case we have more outputs than inputs, we'll clear any output
//    // channels that didn't contain input data, (because these aren't
//    // guaranteed to be empty - they may contain garbage).
//    for (int i = getTotalNumInputChannels()/*getNumInputChannels()*/; i < getTotalNumOutputChannels()/*getNumOutputChannels()*/; ++i)
//        buffer.clear(i, 0, buffer.getNumSamples());
//    
//    // if the plugin is initialized by prepareToPlay()
//    if (inited)
//    {   // lock_guard does not seem to work reliably
//        //std::lock_guard<std::mutex> lockInitLock (initLock); // screw it, stalling here if need be is better than a crash.  the only other time this lock can be held is when prepareToPlay() is called
//        //initLock.lock();
//        
//        // need to update block size if it is not what we expected to make sure we have enough memory alloced for processing
//        if (N != buffer.getNumSamples())
//        {
//            N = buffer.getNumSamples();
//            int playableSourceMaxBufferSize = N;
//            // also got to reset the resampler to the new buffer size if the incoming sample rate is not 44.1kHz
//            if (fs != sampleRate_HRTF)
//            {
//                resampler = Resampler(fs, N, sampleRate_HRTF, true);
//                unsamplerCh1 = Resampler(sampleRate_HRTF, N, fs, false);
//                unsamplerCh2 = Resampler(sampleRate_HRTF, N, fs, false);
//                playableSourceMaxBufferSize = resampler.getNoutMax();
//            }
//            for (auto& s : playableSources)
//                s.allocateForMaxBufferSize(playableSourceMaxBufferSize);
//        }
//        
//        // update playback position stuff
//        AudioPlayHead::CurrentPositionInfo positionInfo;
//        // apparently you only want to call this inside this process block and the information returned by it is only valid for the current process block.
//        getPlayHead()->getCurrentPosition(positionInfo);
//        playing = positionInfo.isPlaying;
//        bool looped = false;
//        const float thisBufferDuration = ((float)N)/fs;
//        if (loopRegionBegin >= 0 && loopRegionEnd >= 0)
//        {
//            posSEC = loopRegionBegin + std::fmod(positionInfo.timeInSeconds, loopRegionEnd - loopRegionBegin);
//            if (posSECprev < loopRegionEnd && posSECprev+thisBufferDuration >= loopRegionEnd)
//                looped = true;
//        }
//        else
//            posSEC = positionInfo.timeInSeconds;
//        
//        // if the playback position does not immediately follow the previous one and it wasn't caused by the looping feature, need to reset the doppler buffer state so that no old audio remaining is played back at the new position
//        bool resetProcessingState = false;
//        if (!looped && std::abs(posSECprev+thisBufferDuration - posSEC) > thisBufferDuration)
//            resetProcessingState = true;
//        posSECprev = posSEC;
//        
//        // convert possibly multiple input channels to mono
//        const int numChannels = buffer.getNumChannels();
//        const int currentN = N;
//        float input[currentN];
//        for (int n = 0; n < currentN; ++n)
//            input[n] = 0;
//        const float scale = 1.0 / numChannels;
//        for (int ch = 0; ch < numChannels; ++ch)
//        {
//            for (int n = 0; n < currentN; ++n)
//                input[n] += buffer.getSample(ch, n) * scale;
//            // clear buffer after grabing a local copy of the input and before the heavier processing goes on to avoid garbage output should the processing not finish in time
//            buffer.clear(ch, 0, currentN);
//        }
//        
//        // got to resample to 44.1kHz if input is a different sample rate because the HRIR data is 44.1kHz
//        // NOTE: size is getNoutMax() b/c we can't tell if the buffer will be long or short until we make the resample call below
//        const int resampledMaxSize = resampler.getNoutMax();
//        float inputResampled[resampledMaxSize];
//        if (fs != sampleRate_HRTF)
//        {
//            for (int n = 0; n < resampledMaxSize; ++n)
//                inputResampled[n] = 0;
//            resampler.resampleLinear(&input[0], &inputResampled[0]);
//        }
//        
//        // the net output accumulator for all sources
//        const int outputSize = 2*currentN;
//        float output[outputSize];
//        for (int n = 0; n < outputSize; ++n)
//            output[n] = 0;
//        
//        // the resampled version ...
//        const int resampledNout = resampler.getNout();
//        const int resampledSize = 2*resampledNout;
//        float outputResampled[resampledSize];
//        for (int n = 0; n < resampledSize; ++n)
//            outputResampled[n] = 0;
//        
//        float *inputPtr, *outputPtr;
//        int inputLength;
//        if (fs != sampleRate_HRTF)
//        {
//            inputPtr = &inputResampled[0];
//            outputPtr = &outputResampled[0];
//            inputLength = resampledNout;
//        }
//        else
//        {
//            inputPtr = &input[0];
//            outputPtr = &output[0];
//            inputLength = currentN;
//        }
//        
//        // process the sources
//        {
//            const ScopedTryLock lockSources (sources.getLock());
//            if (lockSources.isLocked())
//            {
//                bool setSourcePosFromPath = false;
//                for (int s = 0; s < (const int)sources.size(); ++s)
//                {   // update the moving source position here for those sources automated on a path
//                    setSourcePosFromPath = false;
//                    if (lockSourcesToPaths && playing)
//                        setSourcePosFromPath = sources[s]->setParametricPosition(posSEC, playableSources[s].prevPathPosIndex);
//                    // serves as a single point of update for the positional state to ensure positional continuity btw buffers
//                    playableSources[s].updateFromSoundSource(*sources[s]);
//                    playableSources[s].setDopplerOn(dopplerOn /*&& setSourcePosFromPath*/, speedOfSound);
//                    if (resetProcessingState)
//                        playableSources[s].resetProcessingState();
//                    if (! playableSources[s].getSourceMuted())
//                        playableSources[s].processAudio(inputPtr, inputLength, outputPtr, realTime);
//                }
//                prevSourcesSize = sources.size();
//            }
//            else
//            {   // failed to get the lock, so just use the previous PlayableSoundSource data to process this buffer
//                for (int s = 0; s < (const int)prevSourcesSize; ++s)
//                {   // compute approximated position if the source was previously moving since we don't have access to the interps of the locked source.  this is crucial to avoid glitches with the dopper effect on, not so important without the doppler as the ocassional glitches aren't noticable
//                    playableSources[s].advancePosition();
//                    if (! playableSources[s].getSourceMuted())
//                        playableSources[s].processAudio(inputPtr, inputLength, outputPtr, realTime);
//                }
//            }
//        }
//        
//        // resample the processed audio back to the original sample rate of the buffer given to us
//        if (fs != sampleRate_HRTF)
//        {
//            unsamplerCh1.unsampleLinear(outputResampled, resampledNout, output);
//            unsamplerCh2.unsampleLinear(&outputResampled[resampledNout], resampledNout, &output[currentN]);
//        }
//        
//        // copy final data to output buffer
//        for (int ch = 0; ch < 2; ++ch)
//            buffer.copyFrom(ch, 0, &output[ch*currentN], currentN, 0.18/*45.0/256.0*/);
//        
//        //initLock.unlock();
//    } // end if inited
//}
//
////==============================================================================
//bool ThreeDAudioProcessor::hasEditor() const
//{
//    return true; // (change this to false if you choose to not supply an editor)
//}
//
//AudioProcessorEditor* ThreeDAudioProcessor::createEditor()
//{
//    ThreeDAudioProcessorEditor* editor = new ThreeDAudioProcessorEditor (this);
//    return editor;
//}
//
////==============================================================================
//void ThreeDAudioProcessor::getStateInformation (MemoryBlock& destData)
//{
//    // You should use this method to store your parameters in the memory block.
//    // You could do that either as raw data, or use the XML or ValueTree classes
//    // as intermediaries to make it easy to save and load complex data.
//    
//    // Here's an example of how you can use XML to make it easy and more robust:
//    
//    // Create an outer XML element..
//    XmlElement xml ("ThreeDAudioPluginSettings");
//    
//    // add some attributes to it..
//    //    // not necessary and probably annoying to save window size with a plugin setting
//    //    xml.setAttribute ("uiWidth", lastUIWidth);
//    //    xml.setAttribute ("uiHeight", lastUIHeight);
//    xml.setAttribute("dopplerOn", dopplerOn);
//    xml.setAttribute("speedOfSound", speedOfSound);
//    xml.setAttribute("loopRegionBegin", loopRegionBegin);
//    xml.setAttribute("loopRegionEnd", loopRegionEnd);
//    xml.setAttribute("processingMode", (int)processingMode.load());
//    // add all the data from the sources array
//    {
//        const ScopedLock lockSources (sources.getLock());
//        for (int s = 0; s < sources.size(); ++s)
//            xml.addChildElement(sources[s]->getXML());
//    }
//    // then use this helper function to stuff it into the binary blob and return it..
//    copyXmlToBinary (xml, destData);
//}
//
//void ThreeDAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
//{
//    // You should use this method to restore your parameters from this memory block,
//    // whose contents will have been created by the getStateInformation() call.
//    // This getXmlFromBinary() helper function retrieves our XML from the binary blob..
//    ScopedPointer<XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
//    if (xmlState != nullptr)
//    {
//        // make sure that it's actually our type of XML object..
//        if (xmlState->hasTagName("ThreeDAudioPluginSettings"))
//        {
//            // ok, now pull out our parameters..
//            //            // not necessary and probably annoying to save window size with a plugin setting
//            //            lastUIWidth  = xmlState->getIntAttribute ("uiWidth", lastUIWidth);
//            //            lastUIHeight = xmlState->getIntAttribute ("uiHeight", lastUIHeight);
//            dopplerOn = xmlState->getBoolAttribute("dopplerOn", false);
//            speedOfSound = xmlState->getDoubleAttribute("speedOfSound", DEFAULT_SPEED_OF_SOUND);
//            loopRegionBegin = xmlState->getDoubleAttribute("loopRegionBegin", -1.0);
//            loopRegionEnd = xmlState->getDoubleAttribute("loopRegionEnd", -1.0);
//            setProcessingMode((ProcessingMode)xmlState->getIntAttribute("processingMode", 2));
//            // restore all the saved sources and their state stuff
//            saveCurrentState(-1);
//            {
//                const ScopedLock lockSources (sources.getLock());
//                for (int s = 0; s < sources.size(); ++s)
//                    delete sources[s];
//                sources.clear();
//                for (int s = 0; s < xmlState->getNumChildElements(); ++s)
//                    sources.add(new SoundSource(xmlState->getChildElement(s)));
//                
//                // a newly loaded preset doesn't update visually for the PATH_AUTOMATION view if we don't do this...
//                if (displayState == DisplayState::PATH_AUTOMATION)
//                    makeSourcesVisibleForPathAutomationView();
//            }
//            saveCurrentState(1);
//        }
//    }
//    //    // update the editor with the new window size loaded from the settings
//    //    // not necessary and probably annoying to save window size with a plugin setting
//    //    if (editor != nullptr) {
//    //        editor->setSize(lastUIWidth, lastUIHeight);
//    //    }
//}
//
////==============================================================================
//// This creates new instances of the plugin..
//AudioProcessor* JUCE_CALLTYPE createPluginFilter()
//{
//    return new ThreeDAudioProcessor();
//}

