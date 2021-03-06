#include "HcalSimpleReconstructor.h"
#include "DataFormats/Common/interface/EDCollection.h"
#include "DataFormats/Common/interface/Handle.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "CalibFormats/HcalObjects/interface/HcalCoderDb.h"
#include "CalibFormats/HcalObjects/interface/HcalCalibrations.h"
#include "CalibFormats/HcalObjects/interface/HcalDbService.h"
#include "CalibFormats/HcalObjects/interface/HcalDbRecord.h"
#include "Geometry/CaloTopology/interface/HcalTopology.h"

#include <iostream>
    
HcalSimpleReconstructor::HcalSimpleReconstructor(edm::ParameterSet const& conf):
  reco_(conf.getParameter<bool>("correctForTimeslew"),
	conf.getParameter<bool>("correctForPhaseContainment"),conf.getParameter<double>("correctionPhaseNS")),
  det_(DetId::Hcal),
  inputLabel_(conf.getParameter<edm::InputTag>("digiLabel")),
  dropZSmarkedPassed_(conf.getParameter<bool>("dropZSmarkedPassed")),
  firstSample_(conf.getParameter<int>("firstSample")),
  samplesToAdd_(conf.getParameter<int>("samplesToAdd")),
  tsFromDB_(conf.getParameter<bool>("tsFromDB")),
  upgradeHBHE_(false),
  upgradeHF_(false),
  HFQIE10_(false),
  paramTS(0),
  theTopology(0)
{
  // Intitialize "method 3"
  reco_.setMeth3Params(
	    conf.getParameter<bool>    ("applyTimeSlewM3"),
            conf.getParameter<double>  ("pedestalUpperLimit"),
            conf.getParameter<int>     ("timeSlewParsType"),
            conf.getParameter<std::vector<double> >("timeSlewPars"),
            conf.getParameter<double>  ("respCorrM3")
      );

  // register for data access
  tok_hbheUp_ = consumes<HBHEUpgradeDigiCollection>(inputLabel_);
  tok_hfUp_ = consumes<HFUpgradeDigiCollection>(inputLabel_);
  tok_hfQIE10_ = consumes<QIE10DigiCollection>(inputLabel_);

  tok_hbhe_ = consumes<HBHEDigiCollection>(inputLabel_);
  tok_hf_ = consumes<HFDigiCollection>(inputLabel_);
  tok_ho_ = consumes<HODigiCollection>(inputLabel_);
  tok_calib_ = consumes<HcalCalibDigiCollection>(inputLabel_);

  std::string subd=conf.getParameter<std::string>("Subdetector");
  if(!strcasecmp(subd.c_str(),"upgradeHBHE")) {
     upgradeHBHE_ = true;
     produces<HBHERecHitCollection>();
  }
  else if (!strcasecmp(subd.c_str(),"upgradeHF")) {
     upgradeHF_ = true;
     produces<HFRecHitCollection>();
  }
  else if (!strcasecmp(subd.c_str(),"HFQIE10")) {
     HFQIE10_ = true;
     produces<HFRecHitCollection>();
  }
  else if (!strcasecmp(subd.c_str(),"HO")) {
    subdet_=HcalOuter;
    produces<HORecHitCollection>();
  }  
  else if (!strcasecmp(subd.c_str(),"HBHE")) {
    if( !upgradeHBHE_) {
      subdet_=HcalBarrel;
      produces<HBHERecHitCollection>();
    }
  } 
  else if (!strcasecmp(subd.c_str(),"HF")) {
    if( !upgradeHF_) {
    subdet_=HcalForward;
    produces<HFRecHitCollection>();
    }
  } 
  else {
    std::cout << "HcalSimpleReconstructor is not associated with a specific subdetector!" << std::endl;
  }       
  
}

void HcalSimpleReconstructor::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.setAllowAnything();
  desc.add<bool>("applyTimeSlewM3", true);
  desc.add<double>("pedestalUpperLimit", 2.7); 
  desc.add<int>("timeSlewParsType",3);
  desc.add<std::vector<double>>("timeSlewPars", { 12.2999, -2.19142, 0, 12.2999, -2.19142, 0, 12.2999, -2.19142, 0 });
  desc.add<double>("respCorrM3", 0.95);
  descriptions.add("simpleHbhereco",desc);
}

HcalSimpleReconstructor::~HcalSimpleReconstructor() { 
  delete paramTS;
  delete theTopology;
}

void HcalSimpleReconstructor::beginRun(edm::Run const&r, edm::EventSetup const & es){
  if(tsFromDB_) {
    edm::ESHandle<HcalRecoParams> p;
    es.get<HcalRecoParamsRcd>().get(p);
    paramTS = new HcalRecoParams(*p.product());

    edm::ESHandle<HcalTopology> htopo;
    es.get<HcalRecNumberingRecord>().get(htopo);
    theTopology=new HcalTopology(*htopo);
    paramTS->setTopo(theTopology);

  }
  reco_.beginRun(es);
}

void HcalSimpleReconstructor::endRun(edm::Run const&r, edm::EventSetup const & es){
  if(tsFromDB_ && paramTS) {
    delete paramTS;
    paramTS = 0;
    reco_.endRun();
  }
}


template<class DIGICOLL, class RECHITCOLL> 
void HcalSimpleReconstructor::process(edm::Event& e, const edm::EventSetup& eventSetup, const edm::EDGetTokenT<DIGICOLL> &tok)
{
  // get conditions
  edm::ESHandle<HcalDbService> conditions;
  eventSetup.get<HcalDbRecord>().get(conditions);

  edm::Handle<DIGICOLL> digi;
  e.getByToken(tok,digi);

  // create empty output
  auto rec = std::make_unique<RECHITCOLL>();
  rec->reserve(digi->size());
  // run the algorithm
  int first = firstSample_;
  int toadd = samplesToAdd_;
  typename DIGICOLL::const_iterator i;
  for (i=digi->begin(); i!=digi->end(); i++) {
    HcalDetId cell = i->id();
    DetId detcell=(DetId)cell;
    // rof 27.03.09: drop ZS marked and passed digis:
    if (dropZSmarkedPassed_)
      if (i->zsMarkAndPass()) continue;

    const HcalCalibrations& calibrations=conditions->getHcalCalibrations(cell);
    const HcalQIECoder* channelCoder = conditions->getHcalCoder (cell);
    const HcalQIEShape* shape = conditions->getHcalShape (channelCoder); 
    HcalCoderDb coder (*channelCoder, *shape);

    //>>> firstSample & samplesToAdd
    if(tsFromDB_) {
      const HcalRecoParam* param_ts = paramTS->getValues(detcell.rawId());
      first = param_ts->firstSample();
      toadd = param_ts->samplesToAdd();
    }
    rec->push_back(reco_.reconstruct(*i,first,toadd,coder,calibrations));   
  }
  // return result
  e.put(std::move(rec));
}


void HcalSimpleReconstructor::processUpgrade(edm::Event& e, const edm::EventSetup& eventSetup)
{
  // get conditions
  edm::ESHandle<HcalDbService> conditions;
  eventSetup.get<HcalDbRecord>().get(conditions);

  if(upgradeHBHE_){
   
    edm::Handle<HBHEUpgradeDigiCollection> digi;
    e.getByToken(tok_hbheUp_, digi);

    // create empty output
    auto rec = std::make_unique<HBHERecHitCollection>();
    rec->reserve(digi->size()); 

    // run the algorithm
    int first = firstSample_;
    int toadd = samplesToAdd_;
    HBHEUpgradeDigiCollection::const_iterator i;
    for (i=digi->begin(); i!=digi->end(); i++) {
      HcalDetId cell = i->id();
      DetId detcell=(DetId)cell;
      // rof 27.03.09: drop ZS marked and passed digis:
      if (dropZSmarkedPassed_)
      if (i->zsMarkAndPass()) continue;
      
      const HcalCalibrations& calibrations=conditions->getHcalCalibrations(cell);
      const HcalQIECoder* channelCoder = conditions->getHcalCoder (cell);
      const HcalQIEShape* shape = conditions->getHcalShape (channelCoder); 
      HcalCoderDb coder (*channelCoder, *shape);

      //>>> firstSample & samplesToAdd
      if(tsFromDB_) {
	const HcalRecoParam* param_ts = paramTS->getValues(detcell.rawId());
	first = param_ts->firstSample();
	toadd = param_ts->samplesToAdd();
      }
      rec->push_back(reco_.reconstructHBHEUpgrade(*i,first,toadd,coder,calibrations));

    }

    e.put(std::move(rec)); // put results
  }// End of upgradeHBHE

  if(upgradeHF_){

    edm::Handle<HFUpgradeDigiCollection> digi;
    e.getByToken(tok_hfUp_, digi);

    // create empty output
    auto rec = std::make_unique<HFRecHitCollection>();
    rec->reserve(digi->size()); 

    // run the algorithm
    int first = firstSample_;
    int toadd = samplesToAdd_;
    HFUpgradeDigiCollection::const_iterator i;
    for (i=digi->begin(); i!=digi->end(); i++) {
      HcalDetId cell = i->id();
      DetId detcell=(DetId)cell;
      // rof 27.03.09: drop ZS marked and passed digis:
      if (dropZSmarkedPassed_)
      if (i->zsMarkAndPass()) continue;
      
      const HcalCalibrations& calibrations=conditions->getHcalCalibrations(cell);
      const HcalQIECoder* channelCoder = conditions->getHcalCoder (cell);
      const HcalQIEShape* shape = conditions->getHcalShape (channelCoder); 
      HcalCoderDb coder (*channelCoder, *shape);

      //>>> firstSample & samplesToAdd
      if(tsFromDB_) {
	const HcalRecoParam* param_ts = paramTS->getValues(detcell.rawId());
	first = param_ts->firstSample();
	toadd = param_ts->samplesToAdd();
      }
      rec->push_back(reco_.reconstructHFUpgrade(*i,first,toadd,coder,calibrations));

    }  
    e.put(std::move(rec)); // put results
  }// End of upgradeHF

  if(HFQIE10_){

    edm::Handle<QIE10DigiCollection> digi;
    e.getByToken(tok_hfQIE10_, digi);

    // create empty output
    auto rec = std::make_unique<HFRecHitCollection>();
    rec->reserve(digi->size()); 

    // run the algorithm
    int first = firstSample_;
    int toadd = samplesToAdd_;
    for (QIE10DigiCollection::const_iterator i=digi->begin(); i!=digi->end(); i++) {
      HcalDetId cell = i->id();
      DetId detcell=(DetId)cell;
	  
      //make dataframe
      QIE10DataFrame frame(*i);
	  
      // rof 27.03.09: drop ZS marked and passed digis:
      if (dropZSmarkedPassed_){
        if (frame.zsMarkAndPass()) continue;
      }

      const HcalCalibrations& calibrations=conditions->getHcalCalibrations(cell);
      const HcalQIECoder* channelCoder = conditions->getHcalCoder (cell);
      const HcalQIEShape* shape = conditions->getHcalShape (channelCoder); 
      HcalCoderDb coder (*channelCoder, *shape);

      //>>> firstSample & samplesToAdd
      if(tsFromDB_) {
	const HcalRecoParam* param_ts = paramTS->getValues(detcell.rawId());
	first = param_ts->firstSample();
	toadd = param_ts->samplesToAdd();
      }
      rec->push_back(reco_.reconstructQIE10(frame,first,toadd,coder,calibrations));

    }  
    e.put(std::move(rec)); // put results
  }// End of upgradeHF
  
}



void HcalSimpleReconstructor::produce(edm::Event& e, const edm::EventSetup& eventSetup)
{
  // HACK related to HB- corrections
  if(e.isRealData()) reco_.setForData(e.run());
 
  // What to produce, better to avoid the same subdet Upgrade and regular 
  // rechits "clashes"
  if(upgradeHBHE_ || upgradeHF_ || HFQIE10_) {
      processUpgrade(e, eventSetup);
  } else if (det_==DetId::Hcal) {
    if ((subdet_==HcalBarrel || subdet_==HcalEndcap) && !upgradeHBHE_) {
      process<HBHEDigiCollection, HBHERecHitCollection>(e, eventSetup, tok_hbhe_);
    } else if (subdet_==HcalForward && !upgradeHF_) {
      process<HFDigiCollection, HFRecHitCollection>(e, eventSetup, tok_hf_);
    } else if (subdet_==HcalOuter) {
      process<HODigiCollection, HORecHitCollection>(e, eventSetup, tok_ho_);
    } else if (subdet_==HcalOther && subdetOther_==HcalCalibration) {
      process<HcalCalibDigiCollection, HcalCalibRecHitCollection>(e, eventSetup, tok_calib_);
    }
  } 
}
