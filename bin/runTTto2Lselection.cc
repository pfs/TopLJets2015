#include "TFile.h"
#include "TDatime.h"
#include "TNamed.h"
#include "TMath.h"
#include "TLorentzVector.h"
#include "TChain.h"
#include "TSystem.h"

#include <string>
#include <vector>

#include "HeavyIonsAnalysis/topskim/include/ForestHiTree.h"
#include "HeavyIonsAnalysis/topskim/include/ForestElectrons.h"
#include "HeavyIonsAnalysis/topskim/include/ForestMuons.h"
#include "HeavyIonsAnalysis/topskim/include/ForestPFCands.h"
#include "HeavyIonsAnalysis/topskim/include/ForestJets.h"
#include "HeavyIonsAnalysis/topskim/include/HistTool.h"

#include "fastjet/ClusterSequence.hh"
#include "fastjet/tools/JetMedianBackgroundEstimator.hh"

const bool isDebug = true;

const float jetPtCut  = 30.;
const float jetEtaCut = 2.4;
const float lepPtCut  = 20.;
const float lepEtaCut = 2.1;
//see https://indico.cern.ch/event/803679/contributions/3342407/attachments/1808912/2953435/egm-minipog-190308.pdf
const float eeScaleShift = 6.8182E-2/5.9097E-2;
const int firstEEScaleShiftRun = 327402; 
const float barrelEndcapEta[2]={1.4442,1.5660};
const float csvWP = 0.8838;

using namespace std;
using namespace fastjet;

// index, ntks in svtx, m svtx, csv
typedef std::tuple<int,int,float,float> BtagInfo_t;
static bool orderByBtagInfo(const BtagInfo_t &a, const BtagInfo_t &b)
{
  int ntks_a(std::get<1>(a)), ntks_b(std::get<1>(b));
  if(ntks_a>ntks_b) return true;

  float csv_a(std::get<3>(a)), csv_b(std::get<3>(b));
  if(csv_a>csv_b) return true;
  return false;
}


//
int main(int argc, char* argv[])
{
  TString inURL,outURL;
  bool isMC(false),isPP(false),doSameSign(false);
  for(int i=1;i<argc;i++){
    string arg(argv[i]);
    if(arg.find("--in")!=string::npos && i+1<argc)       { inURL=TString(argv[i+1]); i++;}
    else if(arg.find("--out")!=string::npos && i+1<argc) { outURL=TString(argv[i+1]); i++;}
    else if(arg.find("--mc")!=string::npos)              { isMC=true;  }
    else if(arg.find("--pp")!=string::npos)              { isPP=true;  }
    else if(arg.find("--ss")!=string::npos)              { doSameSign=true;  }
  }

  bool isSingleMuPD( !isMC && inURL.Contains("SkimMuons"));
  bool isSingleElePD( !isMC && inURL.Contains("SkimElectrons"));

  //book some histograms
  HistTool ht;

  //generic histograms
  ht.addHist("l1pt",     new TH1F("l1pt",     ";Leading lepton transverse momentum [GeV];Events",20,20,200));
  ht.addHist("l1eta",    new TH1F("l1eta",    ";Leading lepton pseudo-rapidity;Events",20,0,2.5));
  ht.addHist("l2pt",     new TH1F("l2pt",     ";Sub-lead lepton transverse momentum [GeV];Events",20,20,200));
  ht.addHist("l2eta",    new TH1F("l2eta",    ";Sub-lead lepton pseudo-rapidity;Events",20,0,2.5));
  ht.addHist("mll",      new TH1F("mll",      ";Dilepton invariant mass [GeV];Events",20,20,200));
  ht.addHist("ptll",     new TH1F("ptll",     ";Dilepton transverse momentum [GeV];Events",20,0,200));
  ht.addHist("dphill",   new TH1F("dphill",   ";#Delta#phi(l,l');Events",20,0,3.15));
  ht.addHist("chrho",    new TH1F("chrho",    ";#rho_{ch};Events",25,0,25));
  for(size_t i=0; i<2; i++) {
    TString pf(i==0 ? "tk" : "pf");
    ht.addHist("n"+pf+"jets",    new TH1F("n"+pf+"jets",    ";Jet multiplicity;Events",5,0,5));
    ht.addHist("n"+pf+"bjets",   new TH1F("n"+pf+"bjets",   ";b-jet multiplicity;Events",5,0,5));    
    ht.addHist("n"+pf+"svtx",    new TH1F("n"+pf+"svtx",    ";Secondary vertex multiplicity;Events",5,0,5));
    for(size_t j=1; j<=2; j++){
      TString ppf(j==1 ? "1" : "2");
      ht.addHist(pf+ppf+"jpt",         new TH1F(pf+ppf+"jpt",      ";Jet transverse momentum [GeV];Events",20,30,200));
      ht.addHist(pf+ppf+"jeta",        new TH1F(pf+ppf+"jeta",     ";Jet pseudo-rapidity;Events",20,0,2.5));
      ht.addHist(pf+ppf+"jsvtxm",      new TH1F(pf+ppf+"jsvtxm",   ";Secondary vertex mass;Events",25,0,6));
      ht.addHist(pf+ppf+"jsvtxntk",    new TH1F(pf+ppf+"jsvtxntk", ";Secondary vertex track multiplicity;Events",5,0,5));
      ht.addHist(pf+ppf+"jcsv",        new TH1F(pf+ppf+"jcsv",     ";CSVv2;Events",25,0,1));
    }
  }
  ht.addHist("chreliso",  new TH1F("chreliso", ";Relative PF charged isolation;Leptons",20,0,2.0));
  ht.addHist("phoreliso", new TH1F("phoreliso",";Relative PF photon isolation;Leptons",20,0,1.0));
  ht.addHist("neureliso", new TH1F("neureliso",";Relative PF neutral hadron isolation;Leptons",20,0,1.0));

  //configure leptons
  TChain *lepTree_p     = new TChain(isPP ? "ggHiNtuplizer/EventTree" : "ggHiNtuplizerGED/EventTree");
  lepTree_p->Add(inURL);
  ForestMuons fForestMu(lepTree_p);  
  ForestElectrons fForestEle(lepTree_p);

  //configure PF cands
  TChain *pfCandTree_p  = new TChain("pfcandAnalyzer/pfTree");
  pfCandTree_p->Add(inURL);
  ForestPFCands fForestPF(pfCandTree_p);

  //configure jets
  TChain *jetTree_p     = new TChain(isPP ? "ak4PFJetAnalyzer/t"      : "akPu4CaloJetAnalyzer/t");
  jetTree_p->Add(inURL);
  ForestJets fForestJets(jetTree_p);

  //global variables
  TChain *hiTree_p      = new TChain("hiEvtAnalyzer/HiTree");
  hiTree_p->Add(inURL);
  HiTree fForestTree(hiTree_p);

  //trigger
  TChain *hltTree_p     = new TChain("hltanalysis/HltTree");
  hltTree_p->Add(inURL);
  int etrig(0),mtrig(0);
  if(isPP){
    hltTree_p->SetBranchStatus("HLT_HIL3Mu20_v1",1);
    hltTree_p->SetBranchAddress("HLT_HIL3Mu20_v1",&mtrig);
    hltTree_p->SetBranchStatus("HLT_HIEle20_WPLoose_Gsf_v1",1);
    hltTree_p->SetBranchAddress("HLT_HIEle20_WPLoose_Gsf_v1",&etrig);
  }else{
    hltTree_p->SetBranchStatus("HLT_HIL3Mu15_v1",1);
    hltTree_p->SetBranchAddress("HLT_HIL3Mu15_v1",&mtrig);
    hltTree_p->SetBranchStatus("HLT_HIEle20Gsf_v1",1);
    hltTree_p->SetBranchAddress("HLT_HIEle20Gsf_v1",&etrig);    
  }
    
  Float_t wgtSum(0);
  int nEntries = (int)lepTree_p->GetEntries();  
  int entryDiv = ((int)(nEntries/20));    
  cout << inURL << " has " << nEntries << "events to process" << endl;
  for(int entry = 0; entry < nEntries; entry++){
    
    if(entry%entryDiv == 0 && nEntries >= 10000) std::cout << "Entry # " << entry << "/" << nEntries << std::endl;
    
    lepTree_p->GetEntry(entry);
    pfCandTree_p->GetEntry(entry);
    jetTree_p->GetEntry(entry);    
    hltTree_p->GetEntry(entry);
    hiTree_p->GetEntry(entry);

    wgtSum += fForestTree.weight;

    //first of all require a trigger
    int trig=etrig+mtrig;
    if(trig==0) continue;
    if(!isMC) {
      if(mtrig==0 && isSingleMuPD) continue;
      if(etrig==0 && isSingleElePD) continue;

      //cross triggers are taken only from the single muon PD to avoid double counting
      if(etrig>0 && mtrig>0 && isSingleElePD) continue;
    }

    //apply global filters
    if(!isPP){
      if(TMath::Abs(fForestTree.vz) > 15) continue;
    }

    //select muons
    std::vector<int> muIdx,noIdMuIdx;
    std::vector<TLorentzVector> muP4;
    for(unsigned int muIter = 0; muIter < fForestMu.muPt->size(); ++muIter) {

      //kinematics selection
      TLorentzVector p4(0,0,0,0);
      p4.SetPtEtaPhiM(fForestMu.muPt->at(muIter),fForestMu.muEta->at(muIter),fForestMu.muPhi->at(muIter),0.1057);
      if(TMath::Abs(p4.Eta()) > lepEtaCut) continue;
      if(p4.Pt() < lepPtCut) continue;

      noIdMuIdx.push_back(muIter);

      //id (Tight muon requirements)
      int type=fForestMu.muType->at(muIter);
      bool isGlobal( ((type>>1)&0x1) );
      if(!isGlobal) continue;
      bool isPF( ((type>>5)&0x1) );
      if(!isPF) continue;
      bool isGlobalMuonPromptTight(fForestMu.muChi2NDF->at(muIter)<10. && fForestMu.muMuonHits->at(muIter)>0);
      if(!isGlobalMuonPromptTight) continue;
      if(fForestMu.muStations->at(muIter)<=1) continue;
      if(fForestMu.muTrkLayers->at(muIter) <= 5) continue;
      if(fForestMu.muPixelHits->at(muIter) == 0) continue;
      if(TMath::Abs(fForestMu.muInnerD0->at(muIter)) >=0.2 ) continue;
      if(TMath::Abs(fForestMu.muInnerDz->at(muIter)) >=0.5) continue;

      //selected a good muon
      muIdx.push_back(muIter);
      muP4.push_back(p4);
    }
    
    //select electrons
    //cf. https://twiki.cern.ch/twiki/pub/CMS/HiHighPt2019/HIN_electrons2018_followUp.pdf
    std::vector<int> eleIdx,noIdEleIdx; 
    std::vector<TLorentzVector> eP4;
    for(unsigned int eleIter = 0; eleIter < fForestEle.elePt->size(); ++eleIter) {
      
      //kinematics selection
      TLorentzVector p4(0,0,0,0);
      p4.SetPtEtaPhiM(fForestEle.elePt->at(eleIter),fForestEle.eleEta->at(eleIter),fForestEle.elePhi->at(eleIter),0.000511);
      if(!isPP && fForestTree.run<=firstEEScaleShiftRun) p4 *=eeScaleShift;         
      if(TMath::Abs(p4.Eta()) > lepEtaCut) continue;
      if(TMath::Abs(p4.Eta()) > barrelEndcapEta[0] && TMath::Abs(p4.Eta()) < barrelEndcapEta[1] ) continue;
      if(p4.Pt() < lepPtCut) continue;	      

      noIdEleIdx.push_back(eleIter);

      //electron id
      if(fForestEle.eleMissHits->at(eleIter)>1) continue;
      if(fForestEle.eleEoverPInv->at(eleIter)>=0.3) continue;
      if(fForestEle.eleHoverE->at(eleIter)>=0.2) continue;
      if(TMath::Abs(fForestEle.eledEtaAtVtx->at(eleIter))>=0.1) continue;
      if(TMath::Abs(fForestEle.eledPhiAtVtx->at(eleIter))>=0.2) continue;
      if(fForestEle.eleSigmaIEtaIEta->at(eleIter)>=0.05) continue;
      if(TMath::Abs(fForestEle.eleD0->at(eleIter))>=0.1) continue;
      if(TMath::Abs(fForestEle.eleDz->at(eleIter))>=0.5) continue;

      //selected electron
      eleIdx.push_back(eleIter);
      eP4.push_back(p4);
    }

    int nLep=muIdx.size()+eleIdx.size();
    if(nLep<2) continue;
  
    std::vector<TLorentzVector> selLeptons;
    int dilCode(0);
    int charge(0);    
    TLorentzVector ll;
    if(muP4.size()>1 && mtrig>0) {
      dilCode=13*13;
      ll=muP4[0]+muP4[1];
      selLeptons.push_back(muP4[0]);
      selLeptons.push_back(muP4[1]);
      charge=fForestMu.muCharge->at(muIdx[0])*fForestMu.muCharge->at(muIdx[1]);
    }
    else if(muP4.size()>0 && eP4.size()>0 && (etrig>0 || mtrig>0)) {
      dilCode=11*13;
      ll=muP4[0]+eP4[0];
      selLeptons.push_back(muP4[0]);
      selLeptons.push_back(eP4[0]);
      charge=fForestMu.muCharge->at(muIdx[0])*fForestEle.eleCharge->at(eleIdx[0]);      
    }
    else if(eP4.size()>1 && etrig>0) {
      dilCode=11*11;
      ll=eP4[0]+eP4[1];
      selLeptons.push_back(eP4[0]);
      selLeptons.push_back(eP4[1]);
      charge=fForestEle.eleCharge->at(eleIdx[0])*fForestEle.eleCharge->at(eleIdx[1]);
    }else{
      continue;
    }

    if(ll.M()<20) continue;
    bool isOS(charge<0);
    if(doSameSign ^ isOS) continue;

    bool isZ( dilCode!=11*13 && fabs(ll.M()-91)<15 );
    TString dilCat(dilCode==11*13 ? "em" : (dilCode==11*11 ? "ee" : "mm") );

    //build track jets from PF candidates
    //cross-clean with respect to the selected leptons
    //require at least 2 constituents
    std::vector<TLorentzVector> tkJetsP4;
    std::vector<PseudoJet> pseudoParticles;
    ForestPFCands fForestPF(pfCandTree_p);
    for(size_t ipf=0; ipf<fForestPF.pfId->size(); ipf++) {
      int id(abs(fForestPF.pfId->at(ipf)));

      //pass all neutrals
      if(id==22 || id==130 || id==2112 || id==1 || id==2) continue;
      TLorentzVector p4(0,0,0,0);
      p4.SetPtEtaPhiM(fForestPF.pfPt->at(ipf),fForestPF.pfEta->at(ipf),fForestPF.pfPhi->at(ipf),fForestPF.pfM->at(ipf));

      //some basic kinematic cuts
      if(p4.Pt()<0.5) continue;
      if(fabs(p4.Eta())<2.5) continue;

      PseudoJet ip=PseudoJet(p4.Px(),p4.Py(),p4.Pz(),p4.E());
      ip.set_user_index( ipf );
      pseudoParticles.push_back( ip );
    }
    JetDefinition jet_def(antikt_algorithm, 0.4);
    ClusterSequence cs(pseudoParticles, jet_def);
    Selector sel_rapmax = SelectorAbsRapMax(2.4);
    JetDefinition jet_def_for_rho(kt_algorithm,0.5);
    AreaDefinition area_def(active_area,GhostedAreaSpec(2.4+1.));
    JetMedianBackgroundEstimator bge(sel_rapmax, jet_def_for_rho, area_def);
    bge.set_particles(pseudoParticles);
    float tkrho=bge.rho();

    std::vector<PseudoJet> tkjets = sorted_by_pt(cs.inclusive_jets());
    for(auto j : tkjets) {
      if(j.constituents().size()<2) continue;
      TLorentzVector p4(j.px(),j.py(),j.pz(),j.e());
      if(p4.DeltaR(selLeptons[0])<0.4 || p4.DeltaR(selLeptons[1])<0.4) continue;
      if(fabs(p4.Eta())<2.4) continue;
      tkJetsP4.push_back(p4);
    }
   
    //b-tag jet the track jets by matching in deltaR to leptons
    std::vector<BtagInfo_t> matchedJetsIdx,pfJetsIdx;
    std::vector<TLorentzVector> pfJetsP4;
    int npfjets(0),npfbjets(0); 
    for(int jetIter = 0; jetIter < fForestJets.nref; jetIter++){

      //at least two tracks
      if(fForestJets.trackN[jetIter]<2) continue;

      TLorentzVector jp4(0,0,0,0);
      jp4.SetPtEtaPhiM( fForestJets.jtpt[jetIter],fForestJets.jteta[jetIter],fForestJets.jtphi[jetIter],fForestJets.jtm[jetIter]);

      float csvVal=fForestJets.discr_csvV2[jetIter];
      int nsvtxTk=fForestJets.svtxntrk[jetIter];
      float msvtx=fForestJets.svtxm[jetIter];

      for(size_t ij=0; ij<tkJetsP4.size(); ij++) {
        if(jp4.DeltaR( tkJetsP4[ij] ) >0.4) continue;
        matchedJetsIdx.push_back(std::make_tuple(ij,nsvtxTk,msvtx,csvVal));
        break;
      }

      if(jp4.Pt()<30.) continue;
      if(fabs(jp4.Eta())>2.4) continue;
      if(jp4.DeltaR(selLeptons[0])<0.4 || jp4.DeltaR(selLeptons[1])<0.4) continue;            

      pfJetsIdx.push_back(std::make_tuple(jetIter,nsvtxTk,msvtx,csvVal));
      pfJetsP4.push_back(jp4);
      npfjets  += 1;
      npfbjets += (csvVal>csvWP);
    }
    std::sort(matchedJetsIdx.begin(), matchedJetsIdx.end(), orderByBtagInfo);
    std::sort(pfJetsIdx.begin(),      pfJetsIdx.end(),      orderByBtagInfo);


    //fill control histograms
    std::vector<TString> categs;
    categs.push_back(dilCat);
    if(isZ) categs.push_back(dilCat+"Z");
    bool l1EE(fabs(selLeptons[0].Eta())>barrelEndcapEta[1]);
    bool l2EE(fabs(selLeptons[1].Eta())>barrelEndcapEta[1]);
    TString etaCateg( (!l1EE && !l2EE) ? "BB" : (((l1EE && !l2EE) || (!l1EE && l2EE)) ? "EB" : "EE" ) );
    categs.push_back(dilCat+etaCateg);
    if(isZ) {
      categs.push_back(dilCat+etaCateg+"Z");
      
      //add Z recoiling against jet category here to monitor b-tagging
    }

    //monitor also after run where EE scale shift changed
    if(!isPP){
      TString pf( fForestTree.run>=firstEEScaleShiftRun ? "after" : "before" );
      std::vector<TString> addCategs;
      for(auto c : categs) {addCategs.push_back(c); addCategs.push_back(c+pf); }
      categs=addCategs;
    }

    float plotWgt(isMC ? fForestTree.weight : 1.0);
    ht.fill( "l1pt",      selLeptons[0].Pt(),                       plotWgt, categs);
    ht.fill( "l2pt",      selLeptons[1].Pt(),                       plotWgt, categs);
    ht.fill( "l1eta",     fabs(selLeptons[0].Eta()),                plotWgt, categs);
    ht.fill( "l2eta",     fabs(selLeptons[1].Eta()),                plotWgt, categs);
    ht.fill( "dphill",    fabs(selLeptons[0].DeltaPhi(selLeptons[1])), plotWgt, categs);
    ht.fill( "mll",       ll.M(),                                plotWgt, categs);
    ht.fill( "ptll",      ll.Pt(),                               plotWgt, categs);
    ht.fill( "npfjets",   npfjets,   plotWgt, categs);
    ht.fill( "npfbjets",  npfbjets,  plotWgt, categs);

    for(size_t ij=0; ij<min(matchedJetsIdx.size(),size_t(2)); ij++) {     
      TLorentzVector p4=tkJetsP4[ij];
      float ntks(std::get<1>(matchedJetsIdx[ij]));
      float svm(std::get<2>(matchedJetsIdx[ij]));
      float csv(std::get<3>(matchedJetsIdx[ij]));
      TString ppf(ij==1 ? "1" : "2");
      ht.fill( "tk"+ppf+"jpt",      p4.Pt(),        plotWgt, categs);
      ht.fill( "tk"+ppf+"jeta",     fabs(p4.Eta()), plotWgt, categs);
      ht.fill( "tk"+ppf+"jsvtxm",   ntks,           plotWgt, categs);
      ht.fill( "tk"+ppf+"jsvtxntk", svm,            plotWgt, categs);
      ht.fill( "tk"+ppf+"jcsv",     csv,            plotWgt, categs);
    }
    ht.fill( "tkrho", tkrho,            plotWgt, categs);
    
    for(size_t ij=0; ij<min(pfJetsIdx.size(),size_t(2)); ij++) {     
      TLorentzVector p4=pfJetsP4[ij];
      float ntks(std::get<1>(pfJetsIdx[ij]));
      float svm(std::get<2>(pfJetsIdx[ij]));
      float csv(std::get<3>(pfJetsIdx[ij]));
      TString ppf(ij==1 ? "1" : "2");
      ht.fill( "pf"+ppf+"jpt",      p4.Pt(),        plotWgt, categs);
      ht.fill( "pf"+ppf+"jeta",     fabs(p4.Eta()), plotWgt, categs);
      ht.fill( "pf"+ppf+"jsvtxm",   ntks,           plotWgt, categs);
      ht.fill( "pf"+ppf+"jsvtxntk", svm,            plotWgt, categs);
      ht.fill( "pf"+ppf+"jcsv",     csv,            plotWgt, categs);
    }

  }

  //save histos to file  
  if(outURL!=""){
    TFile *fOut=TFile::Open(outURL,"RECREATE");
    fOut->cd();
    for (auto& it : ht.getPlots())  { 
      if(it.second->GetEntries()==0) continue;
      if(isMC && wgtSum!=0) it.second->Scale(1./wgtSum);
      it.second->SetDirectory(fOut); it.second->Write(); 
    }
    for (auto& it : ht.get2dPlots())  { 
      if(it.second->GetEntries()==0) continue;
      if(isMC && wgtSum!=0) it.second->Scale(1./wgtSum);
      it.second->SetDirectory(fOut); it.second->Write(); 
    }
    fOut->Close();
  }

  return 0;
}