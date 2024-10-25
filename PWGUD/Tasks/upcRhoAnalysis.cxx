// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.
///
/// \brief  task for analysis of rho in UPCs using UD tables (from SG producer)
///         includes event tagging based on ZN information, track selection, reconstruction,
///         and also some basic stuff for decay phi anisotropy studies
/// \author Jakub Juracka, jakub.juracka@cern.ch

#include "Framework/AnalysisTask.h"
#include "Framework/AnalysisDataModel.h"
#include "Framework/runDataProcessing.h"

#include "Math/Vector4D.h" // similiar to TLorentzVector (which is now legacy apparently)
#include "random"

#include "Common/DataModel/PIDResponse.h"

#include "PWGUD/DataModel/UDTables.h"
#include "PWGUD/Core/UPCTauCentralBarrelHelperRL.h"

using namespace o2;
using namespace o2::framework;
using namespace o2::framework::expressions;

using FullUDSgCollision = soa::Join<aod::UDCollisions, aod::UDCollisionsSels, aod::UDZdcsReduced, aod::SGCollisions>::iterator;
using FullUDTracks = soa::Join<aod::UDTracks, aod::UDTracksExtra, aod::UDTracksDCA, aod::UDTracksPID, aod::UDTracksFlags>;

struct upcRhoAnalysis {
  double PcEtaCut = 0.9; // physics coordination recommendation

  Configurable<bool> specifyGapSide{"specifyGapSide", true, "specify gap side for SG/DG produced data"};
  Configurable<int> gapSide{"gapSide", 2, "gap side for SG produced data"};
  Configurable<bool> requireTof{"requireTof", false, "require TOF signal"};

  Configurable<double> collisionsPosZMaxCut{"collisionsPosZMaxCut", 10.0, "max Z position cut on collisions"};
  Configurable<double> ZNcommonEnergyCut{"ZNcommonEnergyCut", 0.0, "ZN common energy cut"};
  Configurable<double> ZNtimeCut{"ZNtimeCut", 2.0, "ZN time cut"};

  Configurable<double> tracksTpcNSigmaPiCut{"tracksTpcNSigmaPiCut", 3.0, "TPC nSigma pion cut"};
  Configurable<double> tracksDcaMaxCut{"tracksDcaMaxCut", 1.0, "max DCA cut on tracks"};

  Configurable<double> systemMassMinCut{"systemMassMinCut", 0.5, "min M cut for reco system"};
  Configurable<double> systemMassMaxCut{"systemMassMaxCut", 1.2, "max M cut for reco system"};
  Configurable<double> systemPtCut{"systemPtMaxCut", 0.1, "max pT cut for reco system"};
  Configurable<double> systemYCut{"systemYCut", 0.9, "rapiditiy cut for reco system"};

  ConfigurableAxis mAxis{"mAxis", {1000, 0.0, 10.0}, "m (GeV/#it{c}^{2})"};
  ConfigurableAxis mCutAxis{"mCutAxis", {70, 0.5, 1.2}, "m (GeV/#it{c}^{2})"};
  ConfigurableAxis ptAxis{"ptAxis", {1000, 0.0, 10.0}, "p_{T} (GeV/#it{c})"};
  ConfigurableAxis ptCutAxis{"ptCutAxis", {300, 0.0, 0.3}, "p_{T} (GeV/#it{c})"};
  ConfigurableAxis pt2Axis{"pt2Axis", {300, 0.0, 0.09}, "p_{T}^{2} (GeV^{2}/#it{c}^{2})"};
  ConfigurableAxis etaAxis{"etaAxis", {180, -0.9, 0.9}, "#eta"};
  ConfigurableAxis yAxis{"yAxis", {180, -0.9, 0.9}, "y"};
  ConfigurableAxis phiAxis{"phiAxis", {180, 0.0, o2::constants::math::TwoPI}, "#phi"};
  ConfigurableAxis phiAsymmAxis{"phiAsymmAxis", {182, -o2::constants::math::PI, o2::constants::math::PI}, "#phi"};
  ConfigurableAxis momentumFromPhiAxis{"momentumFromPhiAxis", {400, -0.1, 0.1}, "p (GeV/#it{c})"};
  ConfigurableAxis ptQuantileAxis{"ptQuantileAxis", {0, 0.0181689, 0.0263408, 0.0330488, 0.0390369, 0.045058, 0.0512604, 0.0582598, 0.066986, 0.0788085, 0.1}, "p_{T} (GeV/#it{c})"};

  HistogramRegistry registry{"registry", {}, OutputObjHandlingPolicy::AnalysisObject};

  void init(o2::framework::InitContext&)
  {
    // QA //
    // collisions
    registry.add("QC/collisions/hPosXY", ";x (cm);y (cm);counts", kTH2D, {{2000, -0.1, 0.1}, {2000, -0.1, 0.1}});
    registry.add("QC/collisions/hPosZ", ";z (cm);counts", kTH1D, {{400, -20.0, 20.0}});
    registry.add("QC/collisions/hNumContrib", ";number of contributors;counts", kTH1D, {{36, -0.5, 35.5}});
    registry.add("QC/collisions/hZdcCommonEnergy", ";ZNA common energy;ZNC common energy;counts", kTH2D, {{250, -5.0, 20.0}, {250, -5.0, 20.0}});
    registry.add("QC/collisions/hZdcTime", ";ZNA time (ns);ZNC time (ns);counts", kTH2D, {{200, -10.0, 10.0}, {200, -10.0, 10.0}});
    registry.add("QC/collisions/hZnaTimeVsCommonEnergy", ";ZNA common energy;ZNA time (ns);counts", kTH2D, {{250, -5.0, 20.0}, {200, -10.0, 10.0}});
    registry.add("QC/collisions/hZncTimeVsCommonEnergy", ";ZNC common energy;ZNC time (ns);counts", kTH2D, {{250, -5.0, 20.0}, {200, -10.0, 10.0}});
    registry.add("QC/collisions/hZnaTimeVsPosZ", ";z (cm);ZNA time (ns);counts", kTH2D, {{400, -20.0, 20.0}, {300, -1.5, 1.5}});
    registry.add("QC/collisions/hZncTimeVsPosZ", ";z (cm);ZNC time (ns);counts", kTH2D, {{400, -20.0, 20.0}, {300, -1.5, 1.5}});
    registry.add("QC/collisions/hPosZVsZnTimeAdd", ";(ZNA time + ZNC time)/2 (ns);z (cm);counts", kTH2D, {{300, -1.5, 1.5}, {400, -20.0, 20.0}});
    registry.add("QC/collisions/hPosZVsZnTimeSub", ";(ZNA time - ZNC time)/2 (ns);z (cm);counts", kTH2D, {{300, -1.5, 1.5}, {400, -20.0, 20.0}});
    // all tracks
    registry.add("QC/tracks/raw/hTpcNSigmaPi", ";TPC n#sigma_{#pi};counts", kTH1D, {{400, -10.0, 30.0}});
    registry.add("QC/tracks/raw/hTofNSigmaPi", ";TOF n#sigma_{#pi};counts", kTH1D, {{400, -20.0, 20.0}});
    registry.add("QC/tracks/raw/hTpcNSigmaEl", ";TPC n#sigma_{e};counts", kTH1D, {{400, -10.0, 30.0}});
    registry.add("QC/tracks/raw/hDcaXYZ", ";DCA_{z} (cm);DCA_{xy} (cm);counts", kTH2D, {{1000, -5.0, 5.0}, {1000, -5.0, 5.0}});
    registry.add("QC/tracks/raw/hItsNCls", ";ITS N_{cls};counts", kTH1D, {{11, -0.5, 10.5}});
    registry.add("QC/tracks/raw/hItsChi2NCl", ";ITS #chi^{2}/N_{cls};counts", kTH1D, {{1000, 0.0, 100.0}});
    registry.add("QC/tracks/raw/hTpcChi2NCl", ";TPC #chi^{2}/N_{cls};counts", kTH1D, {{1000, 0.0, 100.0}});
    registry.add("QC/tracks/raw/hTpcNClsFindable", ";TPC N_{cls} findable;counts", kTH1D, {{200, 0.0, 200.0}});
    registry.add("QC/tracks/raw/hTpcNClsCrossedRows", ";TPC crossed rows;counts", kTH1D, {{200, 0.0, 200.0}});
    // tracks passing selections
    registry.add("QC/tracks/cut/hTpcNSigmaPi2D", ";TPC n#sigma(#pi_{1});TPC n#sigma(#pi_{2});counts", kTH2D, {{400, -10.0, 30.0}, {400, -10.0, 30.0}});
    registry.add("QC/tracks/cut/hTpcNSigmaEl2D", ";TPC n#sigma(e_{1});TPC n#sigma(e_{2});counts", kTH2D, {{400, -10.0, 30.0}, {400, -10.0, 30.0}});
    registry.add("QC/tracks/cut/hTpcSignalVsPt", ";p_{T} (GeV/#it{c});TPC signal;counts", kTH2D, {ptAxis, {500, 0.0, 500.0}});
    registry.add("QC/tracks/cut/hRemainingTracks", ";remaining tracks;counts", kTH1D, {{21, -0.5, 20.5}});
    registry.add("QC/tracks/cut/hDcaXYZ", ";DCA_{z} (cm);DCA_{xy} (cm);counts", kTH2D, {{1000, -5.0, 5.0}, {1000, -5.0, 5.0}});
    // selection counter
    std::vector<std::string> selectionCounterLabels = {"all tracks", "PV contributor", "ITS + TPC hit", "TOF requirement", "DCA cut", "#eta cut", "2D TPC n#sigma_{#pi} cut"};
    auto hSelectionCounter = registry.add<TH1>("QC/tracks/hSelectionCounter", ";;counts", kTH1D, {{static_cast<int>(selectionCounterLabels.size()), -0.5, static_cast<double>(selectionCounterLabels.size()) - 0.5}});
    for (int i = 0; i < static_cast<int>(selectionCounterLabels.size()); ++i)
      hSelectionCounter->GetXaxis()->SetBinLabel(i + 1, selectionCounterLabels[i].c_str());
    // RECO HISTOS //
    // PIONS
    // no selection
    registry.add("pions/no-selection/unlike-sign/hPt", ";p_{T}(#pi_{1}) (GeV/#it{c});p_{T}(#pi_{2}) (GeV/#it{c});counts", kTH2D, {ptAxis, ptAxis});
    registry.add("pions/no-selection/unlike-sign/hEta", ";#eta(#pi_{1});#eta(#pi_{2});counts", kTH2D, {etaAxis, etaAxis});
    registry.add("pions/no-selection/unlike-sign/hPhi", ";#phi(#pi_{1});#phi(#pi_{2});counts", kTH2D, {phiAxis, phiAxis});
    registry.add("pions/no-selection/like-sign/hPt", ";p_{T}(#pi_{1}) (GeV/#it{c});p_{T}(#pi_{2}) (GeV/#it{c});counts", kTH2D, {ptAxis, ptAxis});
    registry.add("pions/no-selection/like-sign/hEta", ";#eta(#pi_{1});#eta(#pi_{2});counts", kTH2D, {etaAxis, etaAxis});
    registry.add("pions/no-selection/like-sign/hPhi", ";#phi(#pi_{1});#phi(#pi_{2});counts", kTH2D, {phiAxis, phiAxis});
    // selected
    registry.add("pions/selected/unlike-sign/hPt", ";p_{T}(#pi_{1}) (GeV/#it{c});p_{T}(#pi_{2}) (GeV/#it{c});counts", kTH2D, {ptAxis, ptAxis});
    registry.add("pions/selected/unlike-sign/hEta", ";#eta(#pi_{1});#eta(#pi_{2});counts", kTH2D, {etaAxis, etaAxis});
    registry.add("pions/selected/unlike-sign/hPhi", ";#phi(#pi_{1});#phi(#pi_{2});counts", kTH2D, {phiAxis, phiAxis});
    registry.add("pions/selected/like-sign/hPt", ";p_{T}(#pi_{1}) (GeV/#it{c});p_{T}(#pi_{2}) (GeV/#it{c});counts", kTH2D, {ptAxis, ptAxis});
    registry.add("pions/selected/like-sign/hEta", ";#eta(#pi_{1});#eta(#pi_{2});counts", kTH2D, {etaAxis, etaAxis});
    registry.add("pions/selected/like-sign/hPhi", ";#phi(#pi_{1});#phi(#pi_{2});counts", kTH2D, {phiAxis, phiAxis});

    // RAW RHOS
    registry.add("system/2pi/raw/unlike-sign/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mAxis});
    registry.add("system/2pi/raw/unlike-sign/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptAxis});
    registry.add("system/2pi/raw/unlike-sign/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mAxis, ptAxis});
    registry.add("system/2pi/raw/unlike-sign/hY", ";y;counts", kTH1D, {yAxis});
    registry.add("system/2pi/raw/like-sign/positive/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mAxis});
    registry.add("system/2pi/raw/like-sign/positive/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptAxis});
    registry.add("system/2pi/raw/like-sign/positive/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mAxis, ptAxis});
    registry.add("system/2pi/raw/like-sign/positive/hY", ";y;counts", kTH1D, {yAxis});
    registry.add("system/2pi/raw/like-sign/negative/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mAxis});
    registry.add("system/2pi/raw/like-sign/negative/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptAxis});
    registry.add("system/2pi/raw/like-sign/negative/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mAxis, ptAxis});
    registry.add("system/2pi/raw/like-sign/negative/hY", ";y;counts", kTH1D, {yAxis});

    // SELECTED RHOS
    // no selection
    registry.add("system/2pi/cut/no-selection/unlike-sign/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mCutAxis});
    registry.add("system/2pi/cut/no-selection/unlike-sign/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptCutAxis});
    registry.add("system/2pi/cut/no-selection/unlike-sign/hPt2", ";p_{T}^{2} (GeV^{2}/#it{c}^{2});counts", kTH1D, {pt2Axis});
    registry.add("system/2pi/cut/no-selection/unlike-sign/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptCutAxis});
    registry.add("system/2pi/cut/no-selection/unlike-sign/hY", ";y;counts", kTH1D, {yAxis});
    registry.add("system/2pi/cut/no-selection/unlike-sign/hPhiRandom", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/no-selection/unlike-sign/hPhiCharge", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/no-selection/unlike-sign/hPhiRandomVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/no-selection/unlike-sign/hPhiChargeVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/no-selection/unlike-sign/hPyVsPxRandom", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/no-selection/unlike-sign/hPyVsPxCharge", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/no-selection/unlike-sign/hMInPtQuantileBins", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptQuantileAxis});

    registry.add("system/2pi/cut/no-selection/like-sign/positive/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mCutAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/positive/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptCutAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/positive/hPt2", ";p_{T}^{2} (GeV^{2}/#it{c}^{2});counts", kTH1D, {pt2Axis});
    registry.add("system/2pi/cut/no-selection/like-sign/positive/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptCutAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/positive/hY", ";y;counts", kTH1D, {yAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/positive/hPhiRandom", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/positive/hPhiCharge", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/positive/hPhiRandomVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/positive/hPhiChargeVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/positive/hPyVsPxRandom", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/positive/hPyVsPxCharge", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/positive/hMInPtQuantileBins", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptQuantileAxis});

    registry.add("system/2pi/cut/no-selection/like-sign/negative/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mCutAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/negative/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptCutAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/negative/hPt2", ";p_{T}^{2} (GeV^{2}/#it{c}^{2});counts", kTH1D, {pt2Axis});
    registry.add("system/2pi/cut/no-selection/like-sign/negative/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptCutAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/negative/hY", ";y;counts", kTH1D, {yAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/negative/hPhiRandom", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/negative/hPhiCharge", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/negative/hPhiRandomVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/negative/hPhiChargeVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/negative/hPyVsPxRandom", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/negative/hPyVsPxCharge", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/no-selection/like-sign/negative/hMInPtQuantileBins", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptQuantileAxis});

    // 0n0n
    registry.add("system/2pi/cut/0n0n/unlike-sign/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mCutAxis});
    registry.add("system/2pi/cut/0n0n/unlike-sign/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptCutAxis});
    registry.add("system/2pi/cut/0n0n/unlike-sign/hPt2", ";p_{T}^{2} (GeV^{2}/#it{c}^{2});counts", kTH1D, {pt2Axis});
    registry.add("system/2pi/cut/0n0n/unlike-sign/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptCutAxis});
    registry.add("system/2pi/cut/0n0n/unlike-sign/hY", ";y;counts", kTH1D, {yAxis});
    registry.add("system/2pi/cut/0n0n/unlike-sign/hPhiRandom", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/0n0n/unlike-sign/hPhiCharge", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/0n0n/unlike-sign/hPhiRandomVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/0n0n/unlike-sign/hPhiChargeVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/0n0n/unlike-sign/hPyVsPxRandom", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/0n0n/unlike-sign/hPyVsPxCharge", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/0n0n/unlike-sign/hMInPtQuantileBins", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptQuantileAxis});

    registry.add("system/2pi/cut/0n0n/like-sign/positive/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mCutAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/positive/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptCutAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/positive/hPt2", ";p_{T}^{2} (GeV^{2}/#it{c}^{2});counts", kTH1D, {pt2Axis});
    registry.add("system/2pi/cut/0n0n/like-sign/positive/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptCutAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/positive/hY", ";y;counts", kTH1D, {yAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/positive/hPhiRandom", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/positive/hPhiCharge", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/positive/hPhiRandomVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/positive/hPhiChargeVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/positive/hPyVsPxRandom", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/positive/hPyVsPxCharge", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/positive/hMInPtQuantileBins", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptQuantileAxis});

    registry.add("system/2pi/cut/0n0n/like-sign/negative/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mCutAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/negative/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptCutAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/negative/hPt2", ";p_{T}^{2} (GeV^{2}/#it{c}^{2});counts", kTH1D, {pt2Axis});
    registry.add("system/2pi/cut/0n0n/like-sign/negative/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptCutAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/negative/hY", ";y;counts", kTH1D, {yAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/negative/hPhiRandom", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/negative/hPhiCharge", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/negative/hPhiRandomVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/negative/hPhiChargeVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/negative/hPyVsPxRandom", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/negative/hPyVsPxCharge", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/0n0n/like-sign/negative/hMInPtQuantileBins", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptQuantileAxis});

    // Xn0n
    registry.add("system/2pi/cut/Xn0n/unlike-sign/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mCutAxis});
    registry.add("system/2pi/cut/Xn0n/unlike-sign/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptCutAxis});
    registry.add("system/2pi/cut/Xn0n/unlike-sign/hPt2", ";p_{T}^{2} (GeV^{2}/#it{c}^{2});counts", kTH1D, {pt2Axis});
    registry.add("system/2pi/cut/Xn0n/unlike-sign/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptCutAxis});
    registry.add("system/2pi/cut/Xn0n/unlike-sign/hY", ";y;counts", kTH1D, {yAxis});
    registry.add("system/2pi/cut/Xn0n/unlike-sign/hPhiRandom", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/Xn0n/unlike-sign/hPhiCharge", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/Xn0n/unlike-sign/hPhiRandomVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/Xn0n/unlike-sign/hPhiChargeVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/Xn0n/unlike-sign/hPyVsPxRandom", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/Xn0n/unlike-sign/hPyVsPxCharge", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/Xn0n/unlike-sign/hMInPtQuantileBins", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptQuantileAxis});

    registry.add("system/2pi/cut/Xn0n/like-sign/positive/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mCutAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/positive/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptCutAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/positive/hPt2", ";p_{T}^{2} (GeV^{2}/#it{c}^{2});counts", kTH1D, {pt2Axis});
    registry.add("system/2pi/cut/Xn0n/like-sign/positive/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptCutAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/positive/hY", ";y;counts", kTH1D, {yAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/positive/hPhiRandom", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/positive/hPhiCharge", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/positive/hPhiRandomVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/positive/hPhiChargeVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/positive/hPyVsPxRandom", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/positive/hPyVsPxCharge", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/positive/hMInPtQuantileBins", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptQuantileAxis});

    registry.add("system/2pi/cut/Xn0n/like-sign/negative/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mCutAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/negative/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptCutAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/negative/hPt2", ";p_{T}^{2} (GeV^{2}/#it{c}^{2});counts", kTH1D, {pt2Axis});
    registry.add("system/2pi/cut/Xn0n/like-sign/negative/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptCutAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/negative/hY", ";y;counts", kTH1D, {yAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/negative/hPhiRandom", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/negative/hPhiCharge", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/negative/hPhiRandomVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/negative/hPhiChargeVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/negative/hPyVsPxRandom", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/negative/hPyVsPxCharge", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/Xn0n/like-sign/negative/hMInPtQuantileBins", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptQuantileAxis});

    // 0nXn
    registry.add("system/2pi/cut/0nXn/unlike-sign/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mCutAxis});
    registry.add("system/2pi/cut/0nXn/unlike-sign/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptCutAxis});
    registry.add("system/2pi/cut/0nXn/unlike-sign/hPt2", ";p_{T}^{2} (GeV^{2}/#it{c}^{2});counts", kTH1D, {pt2Axis});
    registry.add("system/2pi/cut/0nXn/unlike-sign/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptCutAxis});
    registry.add("system/2pi/cut/0nXn/unlike-sign/hY", ";y;counts", kTH1D, {yAxis});
    registry.add("system/2pi/cut/0nXn/unlike-sign/hPhiRandom", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/0nXn/unlike-sign/hPhiCharge", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/0nXn/unlike-sign/hPhiRandomVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/0nXn/unlike-sign/hPhiChargeVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/0nXn/unlike-sign/hPyVsPxRandom", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/0nXn/unlike-sign/hPyVsPxCharge", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/0nXn/unlike-sign/hMInPtQuantileBins", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptQuantileAxis});

    registry.add("system/2pi/cut/0nXn/like-sign/positive/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mCutAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/positive/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptCutAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/positive/hPt2", ";p_{T}^{2} (GeV^{2}/#it{c}^{2});counts", kTH1D, {pt2Axis});
    registry.add("system/2pi/cut/0nXn/like-sign/positive/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptCutAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/positive/hY", ";y;counts", kTH1D, {yAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/positive/hPhiRandom", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/positive/hPhiCharge", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/positive/hPhiRandomVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/positive/hPhiChargeVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/positive/hPyVsPxRandom", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/positive/hPyVsPxCharge", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/positive/hMInPtQuantileBins", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptQuantileAxis});

    registry.add("system/2pi/cut/0nXn/like-sign/negative/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mCutAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/negative/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptCutAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/negative/hPt2", ";p_{T}^{2} (GeV^{2}/#it{c}^{2});counts", kTH1D, {pt2Axis});
    registry.add("system/2pi/cut/0nXn/like-sign/negative/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptCutAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/negative/hY", ";y;counts", kTH1D, {yAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/negative/hPhiRandom", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/negative/hPhiCharge", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/negative/hPhiRandomVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/negative/hPhiChargeVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/negative/hPyVsPxRandom", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/negative/hPyVsPxCharge", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/0nXn/like-sign/negative/hMInPtQuantileBins", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptQuantileAxis});

    // XnXn
    registry.add("system/2pi/cut/XnXn/unlike-sign/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mCutAxis});
    registry.add("system/2pi/cut/XnXn/unlike-sign/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptCutAxis});
    registry.add("system/2pi/cut/XnXn/unlike-sign/hPt2", ";p_{T}^{2} (GeV^{2}/#it{c}^{2});counts", kTH1D, {pt2Axis});
    registry.add("system/2pi/cut/XnXn/unlike-sign/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptCutAxis});
    registry.add("system/2pi/cut/XnXn/unlike-sign/hY", ";y;counts", kTH1D, {yAxis});
    registry.add("system/2pi/cut/XnXn/unlike-sign/hPhiRandom", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/XnXn/unlike-sign/hPhiCharge", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/XnXn/unlike-sign/hPhiRandomVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/XnXn/unlike-sign/hPhiChargeVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/XnXn/unlike-sign/hPyVsPxRandom", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/XnXn/unlike-sign/hPyVsPxCharge", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/XnXn/unlike-sign/hMInPtQuantileBins", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptQuantileAxis});

    registry.add("system/2pi/cut/XnXn/like-sign/positive/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mCutAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/positive/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptCutAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/positive/hPt2", ";p_{T}^{2} (GeV^{2}/#it{c}^{2});counts", kTH1D, {pt2Axis});
    registry.add("system/2pi/cut/XnXn/like-sign/positive/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptCutAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/positive/hY", ";y;counts", kTH1D, {yAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/positive/hPhiRandom", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/positive/hPhiCharge", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/positive/hPhiRandomVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/positive/hPhiChargeVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/positive/hPyVsPxRandom", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/positive/hPyVsPxCharge", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/positive/hMInPtQuantileBins", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptQuantileAxis});

    registry.add("system/2pi/cut/XnXn/like-sign/negative/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mCutAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/negative/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptCutAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/negative/hPt2", ";p_{T}^{2} (GeV^{2}/#it{c}^{2});counts", kTH1D, {pt2Axis});
    registry.add("system/2pi/cut/XnXn/like-sign/negative/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptCutAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/negative/hY", ";y;counts", kTH1D, {yAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/negative/hPhiRandom", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/negative/hPhiCharge", ";#phi;counts", kTH1D, {phiAsymmAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/negative/hPhiRandomVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/negative/hPhiChargeVsM", ";m (GeV/#it{c}^{2});#phi;counts", kTH2D, {mCutAxis, phiAsymmAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/negative/hPyVsPxRandom", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/negative/hPyVsPxCharge", ";p_{x} (GeV/#it{c});p_{y} (GeV/#it{c});counts", kTH2D, {momentumFromPhiAxis, momentumFromPhiAxis});
    registry.add("system/2pi/cut/XnXn/like-sign/negative/hMInPtQuantileBins", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mCutAxis, ptQuantileAxis});

    // 4PI AND 6PI SYSTEM
    registry.add("system/4pi/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mAxis});
    registry.add("system/4pi/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptAxis});
    registry.add("system/4pi/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mAxis, ptAxis});
    registry.add("system/4pi/hY", ";y;counts", kTH1D, {yAxis});
    registry.add("system/6pi/hM", ";m (GeV/#it{c}^{2});counts", kTH1D, {mAxis});
    registry.add("system/6pi/hPt", ";p_{T} (GeV/#it{c});counts", kTH1D, {ptAxis});
    registry.add("system/6pi/hPtVsM", ";m (GeV/#it{c}^{2});p_{T} (GeV/#it{c});counts", kTH2D, {mAxis, ptAxis});
    registry.add("system/6pi/hY", ";y;counts", kTH1D, {yAxis});
  }

  template <typename C>
  bool collisionPassesCuts(const C& collision) // collision cuts
  {
    if (std::abs(collision.posZ()) > collisionsPosZMaxCut)
      return false;
    if (specifyGapSide && collision.gapSide() != gapSide)
      return false;
    return true;
  }

  template <typename T>
  bool trackPassesCuts(const T& track) // track cuts (PID done separately)
  {
    if (!track.isPVContributor())
      return false;
    registry.fill(HIST("QC/tracks/hSelectionCounter"), 1);
    if (!track.hasITS() || !track.hasTPC())
      return false;
    registry.fill(HIST("QC/tracks/hSelectionCounter"), 2);
    if (requireTof && !track.hasTOF())
      return false;
    registry.fill(HIST("QC/tracks/hSelectionCounter"), 3);
    if (std::abs(track.dcaZ()) > tracksDcaMaxCut || std::abs(track.dcaXY()) > (0.0182 + 0.0350 / std::pow(track.pt(), 1.01))) // Run 2 dynamic DCA cut
      return false;
    registry.fill(HIST("QC/tracks/hSelectionCounter"), 4);
    if (std::abs(eta(track.px(), track.py(), track.pz())) > PcEtaCut)
      return false;
    registry.fill(HIST("QC/tracks/hSelectionCounter"), 5);
    return true;
  }

  template <typename T>
  bool tracksPassPiPID(const T& cutTracks) // n-dimensional PID cut
  {
    double radius = 0.0;
    for (const auto& track : cutTracks)
      radius += std::pow(track.tpcNSigmaPi(), 2);
    return radius < std::pow(tracksTpcNSigmaPiCut, 2);
  }

  template <typename T>
  double tracksTotalCharge(const T& cutTracks) // total charge of selected tracks
  {
    double charge = 0.0;
    for (const auto& track : cutTracks)
      charge += track.sign();
    return charge;
  }

  bool systemPassCuts(const ROOT::Math::PxPyPzMVector& system) // system cuts
  {
    if (system.M() < systemMassMinCut || system.M() > systemMassMaxCut)
      return false;
    if (system.Pt() > systemPtCut)
      return false;
    if (std::abs(system.Rapidity()) > systemYCut)
      return false;
    return true;
  }

  ROOT::Math::PxPyPzMVector reconstructSystem(const std::vector<ROOT::Math::PxPyPzMVector>& cutTracks4Vecs) // reconstruct system from 4-vectors
  {
    ROOT::Math::PxPyPzMVector system;
    for (const auto& track4Vec : cutTracks4Vecs)
      system += track4Vec;
    return system;
  }

  double deltaPhi(const ROOT::Math::PxPyPzMVector& p1, const ROOT::Math::PxPyPzMVector& p2)
  {
    double dPhi = p1.Phi() - p2.Phi();
    if (dPhi > o2::constants::math::PI)
      dPhi -= o2::constants::math::TwoPI;
    else if (dPhi < -o2::constants::math::PI)
      dPhi += o2::constants::math::TwoPI;
    return dPhi; // calculate delta phi in (-pi, pi)
  }

  double getPhiRandom(const std::vector<ROOT::Math::PxPyPzMVector>& cutTracks4Vecs) // decay phi anisotropy
  {                                                                                 // two possible definitions of phi: randomize the tracks
    std::vector<int> indices = {0, 1};
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();    // get time-based seed
    std::shuffle(indices.begin(), indices.end(), std::default_random_engine(seed)); // shuffle indices
    // calculate phi
    ROOT::Math::PxPyPzMVector pOne = cutTracks4Vecs[indices[0]];
    ROOT::Math::PxPyPzMVector pTwo = cutTracks4Vecs[indices[1]];
    ROOT::Math::PxPyPzMVector pPlus = pOne + pTwo;
    ROOT::Math::PxPyPzMVector pMinus = pOne - pTwo;
    return deltaPhi(pPlus, pMinus);
  }

  template <typename T>
  double getPhiCharge(const T& cutTracks, const std::vector<ROOT::Math::PxPyPzMVector>& cutTracks4Vecs)
  { // two possible definitions of phi: charge-based assignment
    ROOT::Math::PxPyPzMVector pOne, pTwo;
    if (cutTracks[0].sign() > 0) {
      pOne = cutTracks4Vecs[0];
      pTwo = cutTracks4Vecs[1];
    } else {
      pOne = cutTracks4Vecs[1];
      pTwo = cutTracks4Vecs[0];
    }
    ROOT::Math::PxPyPzMVector pPlus = pOne + pTwo;
    ROOT::Math::PxPyPzMVector pMinus = pOne - pTwo;
    return deltaPhi(pPlus, pMinus);
  }

  void processReco(FullUDSgCollision const& collision, FullUDTracks const& tracks)
  {
    // QC histograms
    registry.fill(HIST("QC/collisions/hPosXY"), collision.posX(), collision.posY());
    registry.fill(HIST("QC/collisions/hPosZ"), collision.posZ());
    registry.fill(HIST("QC/collisions/hZdcCommonEnergy"), collision.energyCommonZNA(), collision.energyCommonZNC());
    registry.fill(HIST("QC/collisions/hZdcTime"), collision.timeZNA(), collision.timeZNC());
    registry.fill(HIST("QC/collisions/hZnaTimeVsCommonEnergy"), collision.energyCommonZNA(), collision.timeZNA());
    registry.fill(HIST("QC/collisions/hZncTimeVsCommonEnergy"), collision.energyCommonZNC(), collision.timeZNC());
    registry.fill(HIST("QC/collisions/hNumContrib"), collision.numContrib());
    registry.fill(HIST("QC/collisions/hZnaTimeVsPosZ"), collision.posZ(), collision.timeZNA());
    registry.fill(HIST("QC/collisions/hZncTimeVsPosZ"), collision.posZ(), collision.timeZNC());
    registry.fill(HIST("QC/collisions/hPosZVsZnTimeAdd"), (collision.timeZNA() + collision.timeZNC()) / 2., collision.posZ());
    registry.fill(HIST("QC/collisions/hPosZVsZnTimeSub"), (collision.timeZNA() - collision.timeZNC()) / 2., collision.posZ());

    if (!collisionPassesCuts(collision))
      return;

    // event tagging
    bool XnXn = false, OnOn = false, XnOn = false, OnXn = false; // note: On == 0n...
    if (collision.energyCommonZNA() < ZNcommonEnergyCut && collision.energyCommonZNC() < ZNcommonEnergyCut)
      OnOn = true;
    if (collision.energyCommonZNA() > ZNcommonEnergyCut && std::abs(collision.timeZNA()) < ZNtimeCut &&
        collision.energyCommonZNC() > ZNcommonEnergyCut && std::abs(collision.timeZNC()) < ZNtimeCut)
      XnXn = true;
    if (collision.energyCommonZNA() > ZNcommonEnergyCut && std::abs(collision.timeZNA()) < ZNtimeCut && collision.energyCommonZNC() < ZNcommonEnergyCut)
      XnOn = true;
    if (collision.energyCommonZNA() < ZNcommonEnergyCut && collision.energyCommonZNC() > ZNcommonEnergyCut && std::abs(collision.timeZNC()) < ZNtimeCut)
      OnXn = true;
    // vectors for storing selected tracks and their 4-vectors
    std::vector<decltype(tracks.begin())> cutTracks;
    std::vector<ROOT::Math::PxPyPzMVector> cutTracks4Vecs;

    int trackCounter = 0;
    for (const auto& track : tracks) {
      registry.fill(HIST("QC/tracks/raw/hTpcNSigmaPi"), track.tpcNSigmaPi());
      registry.fill(HIST("QC/tracks/raw/hTofNSigmaPi"), track.tofNSigmaPi());
      registry.fill(HIST("QC/tracks/raw/hTpcNSigmaEl"), track.tpcNSigmaEl());
      registry.fill(HIST("QC/tracks/raw/hDcaXYZ"), track.dcaZ(), track.dcaXY());
      registry.fill(HIST("QC/tracks/raw/hItsNCls"), track.itsNCls());
      registry.fill(HIST("QC/tracks/raw/hItsChi2NCl"), track.itsChi2NCl());
      registry.fill(HIST("QC/tracks/raw/hTpcChi2NCl"), track.tpcChi2NCl());
      registry.fill(HIST("QC/tracks/raw/hTpcNClsFindable"), track.tpcNClsFindable());
      registry.fill(HIST("QC/tracks/raw/hTpcNClsCrossedRows"), track.tpcNClsCrossedRows());
      registry.fill(HIST("QC/tracks/hSelectionCounter"), 0);

      if (!trackPassesCuts(track))
        continue;
      trackCounter++;
      cutTracks.push_back(track);
      cutTracks4Vecs.push_back(ROOT::Math::PxPyPzMVector(track.px(), track.py(), track.pz(), o2::constants::physics::MassPionCharged)); // apriori assume pion mass
      registry.fill(HIST("QC/tracks/cut/hTpcSignalVsPt"), track.pt(), track.tpcSignal());
      registry.fill(HIST("QC/tracks/cut/hDcaXYZ"), track.dcaZ(), track.dcaXY());
    }
    registry.fill(HIST("QC/tracks/cut/hRemainingTracks"), trackCounter);

    if (cutTracks.size() == 2) {
      registry.fill(HIST("QC/tracks/cut/hTpcNSigmaPi2D"), cutTracks[0].tpcNSigmaPi(), cutTracks[1].tpcNSigmaPi());
      registry.fill(HIST("QC/tracks/cut/hTpcNSigmaEl2D"), cutTracks[0].tpcNSigmaEl(), cutTracks[1].tpcNSigmaEl());
    }

    if (!tracksPassPiPID(cutTracks))
      return;
    registry.fill(HIST("QC/tracks/hSelectionCounter"), 6, 2); // weighted by 2 for track pair
    // reonstruct system and calculate total charge, save commonly used values into variables
    ROOT::Math::PxPyPzMVector system = reconstructSystem(cutTracks4Vecs);
    int totalCharge = tracksTotalCharge(cutTracks);
    int nTracks = cutTracks.size();
    double mass = system.M();
    double pT = system.Pt();
    double pTsquare = pT * pT;
    double rapidity = system.Rapidity();

    if (nTracks == 2) {
      double phiRandom = getPhiRandom(cutTracks4Vecs);
      double phiCharge = getPhiCharge(cutTracks, cutTracks4Vecs);
      // fill raw histograms according to the total charge
      switch (totalCharge) {
        case 0:
          registry.fill(HIST("pions/no-selection/unlike-sign/hPt"), cutTracks4Vecs[0].Pt(), cutTracks4Vecs[1].Pt());
          registry.fill(HIST("pions/no-selection/unlike-sign/hEta"), cutTracks4Vecs[0].Eta(), cutTracks4Vecs[1].Eta());
          registry.fill(HIST("pions/no-selection/unlike-sign/hPhi"), cutTracks4Vecs[0].Phi() + o2::constants::math::PI, cutTracks4Vecs[1].Phi() + o2::constants::math::PI);
          registry.fill(HIST("system/2pi/raw/unlike-sign/hM"), mass);
          registry.fill(HIST("system/2pi/raw/unlike-sign/hPt"), pT);
          registry.fill(HIST("system/2pi/raw/unlike-sign/hPtVsM"), mass, pT);
          registry.fill(HIST("system/2pi/raw/unlike-sign/hY"), rapidity);
          break;

        case 2:
          registry.fill(HIST("pions/no-selection/like-sign/hPt"), cutTracks4Vecs[0].Pt(), cutTracks4Vecs[1].Pt());
          registry.fill(HIST("pions/no-selection/like-sign/hEta"), cutTracks4Vecs[0].Eta(), cutTracks4Vecs[1].Eta());
          registry.fill(HIST("pions/no-selection/like-sign/hPhi"), cutTracks4Vecs[0].Phi() + o2::constants::math::PI, cutTracks4Vecs[1].Phi() + o2::constants::math::PI);
          registry.fill(HIST("system/2pi/raw/like-sign/positive/hM"), mass);
          registry.fill(HIST("system/2pi/raw/like-sign/positive/hPt"), pT);
          registry.fill(HIST("system/2pi/raw/like-sign/positive/hPtVsM"), mass, pT);
          registry.fill(HIST("system/2pi/raw/like-sign/positive/hY"), rapidity);
          break;

        case -2:
          registry.fill(HIST("pions/no-selection/like-sign/hPt"), cutTracks4Vecs[0].Pt(), cutTracks4Vecs[1].Pt());
          registry.fill(HIST("pions/no-selection/like-sign/hEta"), cutTracks4Vecs[0].Eta(), cutTracks4Vecs[1].Eta());
          registry.fill(HIST("pions/no-selection/like-sign/hPhi"), cutTracks4Vecs[0].Phi() + o2::constants::math::PI, cutTracks4Vecs[1].Phi() + o2::constants::math::PI);
          registry.fill(HIST("system/2pi/raw/like-sign/negative/hM"), mass);
          registry.fill(HIST("system/2pi/raw/like-sign/negative/hPt"), pT);
          registry.fill(HIST("system/2pi/raw/like-sign/negative/hPtVsM"), mass, pT);
          registry.fill(HIST("system/2pi/raw/like-sign/negative/hY"), rapidity);
          break;

        default:
          break;
      }

      // apply cuts to system
      if (!systemPassCuts(system))
        return;

      // fill histograms for system passing cuts
      switch (totalCharge) {
        case 0:
          registry.fill(HIST("pions/selected/unlike-sign/hPt"), cutTracks4Vecs[0].Pt(), cutTracks4Vecs[1].Pt());
          registry.fill(HIST("pions/selected/unlike-sign/hEta"), cutTracks4Vecs[0].Eta(), cutTracks4Vecs[1].Eta());
          registry.fill(HIST("pions/selected/unlike-sign/hPhi"), cutTracks4Vecs[0].Phi() + o2::constants::math::PI, cutTracks4Vecs[1].Phi() + o2::constants::math::PI);
          registry.fill(HIST("system/2pi/cut/no-selection/unlike-sign/hM"), mass);
          registry.fill(HIST("system/2pi/cut/no-selection/unlike-sign/hPt"), pT);
          registry.fill(HIST("system/2pi/cut/no-selection/unlike-sign/hPt2"), pTsquare);
          registry.fill(HIST("system/2pi/cut/no-selection/unlike-sign/hPtVsM"), mass, pT);
          registry.fill(HIST("system/2pi/cut/no-selection/unlike-sign/hY"), rapidity);
          registry.fill(HIST("system/2pi/cut/no-selection/unlike-sign/hPhiRandom"), phiRandom);
          registry.fill(HIST("system/2pi/cut/no-selection/unlike-sign/hPhiCharge"), phiCharge);
          registry.fill(HIST("system/2pi/cut/no-selection/unlike-sign/hPhiRandomVsM"), mass, phiRandom);
          registry.fill(HIST("system/2pi/cut/no-selection/unlike-sign/hPhiChargeVsM"), mass, phiCharge);
          registry.fill(HIST("system/2pi/cut/no-selection/unlike-sign/hPyVsPxRandom"), pT * std::cos(phiRandom), pT * std::sin(phiRandom));
          registry.fill(HIST("system/2pi/cut/no-selection/unlike-sign/hPyVsPxCharge"), pT * std::cos(phiCharge), pT * std::sin(phiCharge));
          registry.fill(HIST("system/2pi/cut/no-selection/unlike-sign/hMInPtQuantileBins"), mass, pT);
          if (OnOn) {
            registry.fill(HIST("system/2pi/cut/0n0n/unlike-sign/hM"), mass);
            registry.fill(HIST("system/2pi/cut/0n0n/unlike-sign/hPt"), pT);
            registry.fill(HIST("system/2pi/cut/0n0n/unlike-sign/hPt2"), pTsquare);
            registry.fill(HIST("system/2pi/cut/0n0n/unlike-sign/hPtVsM"), mass, pT);
            registry.fill(HIST("system/2pi/cut/0n0n/unlike-sign/hY"), rapidity);
            registry.fill(HIST("system/2pi/cut/0n0n/unlike-sign/hPhiRandom"), phiRandom);
            registry.fill(HIST("system/2pi/cut/0n0n/unlike-sign/hPhiCharge"), phiCharge);
            registry.fill(HIST("system/2pi/cut/0n0n/unlike-sign/hPhiRandomVsM"), mass, phiRandom);
            registry.fill(HIST("system/2pi/cut/0n0n/unlike-sign/hPhiChargeVsM"), mass, phiCharge);
            registry.fill(HIST("system/2pi/cut/0n0n/unlike-sign/hPyVsPxRandom"), pT * std::cos(phiRandom), pT * std::sin(phiRandom));
            registry.fill(HIST("system/2pi/cut/0n0n/unlike-sign/hPyVsPxCharge"), pT * std::cos(phiCharge), pT * std::sin(phiCharge));
            registry.fill(HIST("system/2pi/cut/0n0n/unlike-sign/hMInPtQuantileBins"), mass, pT);
          } else if (XnOn) {
            registry.fill(HIST("system/2pi/cut/Xn0n/unlike-sign/hM"), mass);
            registry.fill(HIST("system/2pi/cut/Xn0n/unlike-sign/hPt"), pT);
            registry.fill(HIST("system/2pi/cut/Xn0n/unlike-sign/hPt2"), pTsquare);
            registry.fill(HIST("system/2pi/cut/Xn0n/unlike-sign/hPtVsM"), mass, pT);
            registry.fill(HIST("system/2pi/cut/Xn0n/unlike-sign/hY"), rapidity);
            registry.fill(HIST("system/2pi/cut/Xn0n/unlike-sign/hPhiRandom"), phiRandom);
            registry.fill(HIST("system/2pi/cut/Xn0n/unlike-sign/hPhiCharge"), phiCharge);
            registry.fill(HIST("system/2pi/cut/Xn0n/unlike-sign/hPhiRandomVsM"), mass, phiRandom);
            registry.fill(HIST("system/2pi/cut/Xn0n/unlike-sign/hPhiChargeVsM"), mass, phiCharge);
            registry.fill(HIST("system/2pi/cut/Xn0n/unlike-sign/hPyVsPxRandom"), pT * std::cos(phiRandom), pT * std::sin(phiRandom));
            registry.fill(HIST("system/2pi/cut/Xn0n/unlike-sign/hPyVsPxCharge"), pT * std::cos(phiCharge), pT * std::sin(phiCharge));
            registry.fill(HIST("system/2pi/cut/Xn0n/unlike-sign/hMInPtQuantileBins"), mass, pT);
          } else if (OnXn) {
            registry.fill(HIST("system/2pi/cut/0nXn/unlike-sign/hM"), mass);
            registry.fill(HIST("system/2pi/cut/0nXn/unlike-sign/hPt"), pT);
            registry.fill(HIST("system/2pi/cut/0nXn/unlike-sign/hPt2"), pTsquare);
            registry.fill(HIST("system/2pi/cut/0nXn/unlike-sign/hPtVsM"), mass, pT);
            registry.fill(HIST("system/2pi/cut/0nXn/unlike-sign/hY"), rapidity);
            registry.fill(HIST("system/2pi/cut/0nXn/unlike-sign/hPhiRandom"), phiRandom);
            registry.fill(HIST("system/2pi/cut/0nXn/unlike-sign/hPhiCharge"), phiCharge);
            registry.fill(HIST("system/2pi/cut/0nXn/unlike-sign/hPhiRandomVsM"), mass, phiRandom);
            registry.fill(HIST("system/2pi/cut/0nXn/unlike-sign/hPhiChargeVsM"), mass, phiCharge);
            registry.fill(HIST("system/2pi/cut/0nXn/unlike-sign/hPyVsPxRandom"), pT * std::cos(phiRandom), pT * std::sin(phiRandom));
            registry.fill(HIST("system/2pi/cut/0nXn/unlike-sign/hPyVsPxCharge"), pT * std::cos(phiCharge), pT * std::sin(phiCharge));
            registry.fill(HIST("system/2pi/cut/0nXn/unlike-sign/hMInPtQuantileBins"), mass, pT);
          } else if (XnXn) {
            registry.fill(HIST("system/2pi/cut/XnXn/unlike-sign/hM"), mass);
            registry.fill(HIST("system/2pi/cut/XnXn/unlike-sign/hPt"), pT);
            registry.fill(HIST("system/2pi/cut/XnXn/unlike-sign/hPt2"), pTsquare);
            registry.fill(HIST("system/2pi/cut/XnXn/unlike-sign/hPtVsM"), mass, pT);
            registry.fill(HIST("system/2pi/cut/XnXn/unlike-sign/hY"), rapidity);
            registry.fill(HIST("system/2pi/cut/XnXn/unlike-sign/hPhiRandom"), phiRandom);
            registry.fill(HIST("system/2pi/cut/XnXn/unlike-sign/hPhiCharge"), phiCharge);
            registry.fill(HIST("system/2pi/cut/XnXn/unlike-sign/hPhiRandomVsM"), mass, phiRandom);
            registry.fill(HIST("system/2pi/cut/XnXn/unlike-sign/hPhiChargeVsM"), mass, phiCharge);
            registry.fill(HIST("system/2pi/cut/XnXn/unlike-sign/hPyVsPxRandom"), pT * std::cos(phiRandom), pT * std::sin(phiRandom));
            registry.fill(HIST("system/2pi/cut/XnXn/unlike-sign/hPyVsPxCharge"), pT * std::cos(phiCharge), pT * std::sin(phiCharge));
            registry.fill(HIST("system/2pi/cut/XnXn/unlike-sign/hMInPtQuantileBins"), mass, pT);
          }
          break;

        case 2:
          registry.fill(HIST("pions/selected/like-sign/hPt"), cutTracks4Vecs[0].Pt(), cutTracks4Vecs[1].Pt());
          registry.fill(HIST("pions/selected/like-sign/hEta"), cutTracks4Vecs[0].Eta(), cutTracks4Vecs[1].Eta());
          registry.fill(HIST("pions/selected/like-sign/hPhi"), cutTracks4Vecs[0].Phi() + o2::constants::math::PI, cutTracks4Vecs[1].Phi() + o2::constants::math::PI);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/positive/hM"), mass);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/positive/hPt"), pT);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/positive/hPt2"), pTsquare);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/positive/hPtVsM"), mass, pT);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/positive/hY"), rapidity);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/positive/hPhiRandom"), phiRandom);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/positive/hPhiCharge"), phiCharge);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/positive/hPhiRandomVsM"), mass, phiRandom);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/positive/hPhiChargeVsM"), mass, phiCharge);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/positive/hPyVsPxRandom"), pT * std::cos(phiRandom), pT * std::sin(phiRandom));
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/positive/hPyVsPxCharge"), pT * std::cos(phiCharge), pT * std::sin(phiCharge));
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/positive/hMInPtQuantileBins"), mass, pT);
          if (OnOn) {
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/positive/hM"), mass);
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/positive/hPt"), pT);
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/positive/hPt2"), pTsquare);
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/positive/hPtVsM"), mass, pT);
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/positive/hY"), rapidity);
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/positive/hPhiRandom"), phiRandom);
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/positive/hPhiCharge"), phiCharge);
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/positive/hPhiRandomVsM"), mass, phiRandom);
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/positive/hPhiChargeVsM"), mass, phiCharge);
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/positive/hPyVsPxRandom"), pT * std::cos(phiRandom), pT * std::sin(phiRandom));
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/positive/hPyVsPxCharge"), pT * std::cos(phiCharge), pT * std::sin(phiCharge));
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/positive/hMInPtQuantileBins"), mass, pT);
          } else if (XnOn) {
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/positive/hM"), mass);
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/positive/hPt"), pT);
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/positive/hPt2"), pTsquare);
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/positive/hPtVsM"), mass, pT);
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/positive/hY"), rapidity);
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/positive/hPhiRandom"), phiRandom);
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/positive/hPhiCharge"), phiCharge);
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/positive/hPhiRandomVsM"), mass, phiRandom);
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/positive/hPhiChargeVsM"), mass, phiCharge);
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/positive/hPyVsPxRandom"), pT * std::cos(phiRandom), pT * std::sin(phiRandom));
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/positive/hPyVsPxCharge"), pT * std::cos(phiCharge), pT * std::sin(phiCharge));
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/positive/hMInPtQuantileBins"), mass, pT);
          } else if (OnXn) {
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/positive/hM"), mass);
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/positive/hPt"), pT);
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/positive/hPt2"), pTsquare);
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/positive/hPtVsM"), mass, pT);
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/positive/hY"), rapidity);
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/positive/hPhiRandom"), phiRandom);
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/positive/hPhiCharge"), phiCharge);
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/positive/hPhiRandomVsM"), mass, phiRandom);
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/positive/hPhiChargeVsM"), mass, phiCharge);
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/positive/hPyVsPxRandom"), pT * std::cos(phiRandom), pT * std::sin(phiRandom));
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/positive/hPyVsPxCharge"), pT * std::cos(phiCharge), pT * std::sin(phiCharge));
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/positive/hMInPtQuantileBins"), mass, pT);
          } else if (XnXn) {
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/positive/hM"), mass);
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/positive/hPt"), pT);
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/positive/hPt2"), pTsquare);
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/positive/hPtVsM"), mass, pT);
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/positive/hY"), rapidity);
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/positive/hPhiRandom"), phiRandom);
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/positive/hPhiCharge"), phiCharge);
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/positive/hPhiRandomVsM"), mass, phiRandom);
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/positive/hPhiChargeVsM"), mass, phiCharge);
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/positive/hPyVsPxRandom"), pT * std::cos(phiRandom), pT * std::sin(phiRandom));
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/positive/hPyVsPxCharge"), pT * std::cos(phiCharge), pT * std::sin(phiCharge));
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/positive/hMInPtQuantileBins"), mass, pT);
          }
          break;

        case -2:
          registry.fill(HIST("pions/selected/like-sign/hPt"), cutTracks4Vecs[0].Pt(), cutTracks4Vecs[1].Pt());
          registry.fill(HIST("pions/selected/like-sign/hEta"), cutTracks4Vecs[0].Eta(), cutTracks4Vecs[1].Eta());
          registry.fill(HIST("pions/selected/like-sign/hPhi"), cutTracks4Vecs[0].Phi() + o2::constants::math::PI, cutTracks4Vecs[1].Phi() + o2::constants::math::PI);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/negative/hM"), mass);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/negative/hPt"), pT);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/negative/hPt2"), pTsquare);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/negative/hPtVsM"), mass, pT);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/negative/hY"), rapidity);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/negative/hPhiRandom"), phiRandom);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/negative/hPhiCharge"), phiCharge);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/negative/hPhiRandomVsM"), mass, phiRandom);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/negative/hPhiChargeVsM"), mass, phiCharge);
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/negative/hPyVsPxRandom"), pT * std::cos(phiRandom), pT * std::sin(phiRandom));
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/negative/hPyVsPxCharge"), pT * std::cos(phiCharge), pT * std::sin(phiCharge));
          registry.fill(HIST("system/2pi/cut/no-selection/like-sign/negative/hMInPtQuantileBins"), mass, pT);
          if (OnOn) {
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/negative/hM"), mass);
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/negative/hPt"), pT);
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/negative/hPt2"), pTsquare);
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/negative/hPtVsM"), mass, pT);
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/negative/hY"), rapidity);
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/negative/hPhiRandom"), phiRandom);
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/negative/hPhiCharge"), phiCharge);
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/negative/hPhiRandomVsM"), mass, phiRandom);
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/negative/hPhiChargeVsM"), mass, phiCharge);
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/negative/hPyVsPxRandom"), pT * std::cos(phiRandom), pT * std::sin(phiRandom));
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/negative/hPyVsPxCharge"), pT * std::cos(phiCharge), pT * std::sin(phiCharge));
            registry.fill(HIST("system/2pi/cut/0n0n/like-sign/negative/hMInPtQuantileBins"), mass, pT);
          } else if (XnOn) {
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/negative/hM"), mass);
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/negative/hPt"), pT);
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/negative/hPt2"), pTsquare);
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/negative/hPtVsM"), mass, pT);
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/negative/hY"), rapidity);
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/negative/hPhiRandom"), phiRandom);
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/negative/hPhiCharge"), phiCharge);
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/negative/hPhiRandomVsM"), mass, phiRandom);
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/negative/hPhiChargeVsM"), mass, phiCharge);
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/negative/hPyVsPxRandom"), pT * std::cos(phiRandom), pT * std::sin(phiRandom));
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/negative/hPyVsPxCharge"), pT * std::cos(phiCharge), pT * std::sin(phiCharge));
            registry.fill(HIST("system/2pi/cut/Xn0n/like-sign/negative/hMInPtQuantileBins"), mass, pT);
          } else if (OnXn) {
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/negative/hM"), mass);
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/negative/hPt"), pT);
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/negative/hPt2"), pTsquare);
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/negative/hPtVsM"), mass, pT);
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/negative/hY"), rapidity);
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/negative/hPhiRandom"), phiRandom);
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/negative/hPhiCharge"), phiCharge);
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/negative/hPhiRandomVsM"), mass, phiRandom);
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/negative/hPhiChargeVsM"), mass, phiCharge);
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/negative/hPyVsPxRandom"), pT * std::cos(phiRandom), pT * std::sin(phiRandom));
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/negative/hPyVsPxCharge"), pT * std::cos(phiCharge), pT * std::sin(phiCharge));
            registry.fill(HIST("system/2pi/cut/0nXn/like-sign/negative/hMInPtQuantileBins"), mass, pT);
          } else if (XnXn) {
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/negative/hM"), mass);
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/negative/hPt"), pT);
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/negative/hPt2"), pTsquare);
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/negative/hPtVsM"), mass, pT);
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/negative/hY"), rapidity);
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/negative/hPhiRandom"), phiRandom);
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/negative/hPhiCharge"), phiCharge);
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/negative/hPhiRandomVsM"), mass, phiRandom);
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/negative/hPhiChargeVsM"), mass, phiCharge);
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/negative/hPyVsPxRandom"), pT * std::cos(phiRandom), pT * std::sin(phiRandom));
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/negative/hPyVsPxCharge"), pT * std::cos(phiCharge), pT * std::sin(phiCharge));
            registry.fill(HIST("system/2pi/cut/XnXn/like-sign/negative/hMInPtQuantileBins"), mass, pT);
          }
          break;

        default:
          break;
      }
    } else if (nTracks == 4 && tracksTotalCharge(cutTracks) == 0) { // 4pi system
      registry.fill(HIST("system/4pi/hM"), mass);
      registry.fill(HIST("system/4pi/hPt"), pT);
      registry.fill(HIST("system/4pi/hPtVsM"), mass, pT);
      registry.fill(HIST("system/4pi/hY"), rapidity);
    } else if (nTracks == 6 && tracksTotalCharge(cutTracks) == 0) { // 6pi system
      registry.fill(HIST("system/6pi/hM"), mass);
      registry.fill(HIST("system/6pi/hPt"), pT);
      registry.fill(HIST("system/6pi/hPtVsM"), mass, pT);
      registry.fill(HIST("system/6pi/hY"), rapidity);
    }
  }
  PROCESS_SWITCH(upcRhoAnalysis, processReco, "analyse reco tracks", true);

  // void processMC(aod::UDMcCollisions::iterator const&, aod::UDMcParticles const& mcparticles)
  // {
  //   // loop over all particles in the event
  //   for (auto const& mcparticle : mcparticles) {
  //     // only consider charged pions
  //     if (std::abs(mcparticle.pdgCode()) != 211)
  //       continue;
  //   }
  // }
  // PROCESS_SWITCH(upcRhoAnalysis, processMC, "analyse MC tracks", false);
};

WorkflowSpec defineDataProcessing(ConfigContext const& cfgc)
{
  return WorkflowSpec{
    o2::framework::adaptAnalysisTask<upcRhoAnalysis>(cfgc)};
}
