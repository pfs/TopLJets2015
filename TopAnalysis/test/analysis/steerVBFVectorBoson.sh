#!/bin/bash

WHAT=$1; 
if [ "$#" -ne 1 ]; then 
    echo "steerVBFVectorBoson.sh <SEL/MERGE/PLOT/WWW>";
    echo "        SEL          - launches selection jobs to the batch, output will contain summary trees and control plots"; 
    echo "        MERGE        - merge output"
    echo "        PLOT         - make plots"
    echo "        WWW          - move plots to web-based are"
    exit 1; 
fi

#to run locally use local as queue + can add "--njobs 8" to use 8 parallel jobs
queue=workday
githash=c29f431
eosdir=/store/cmst3/group/top/RunIIFall17/${githash}
fulllumi=41367
vbflumi=7661
lumiUnc=0.025
outdir=${CMSSW_BASE}/src/TopLJets2015/TopAnalysis/test/analysis/VBFVectorBoson
wwwdir=~/www/VBFVectorBoson


RED='\e[31m'
NC='\e[0m'
case $WHAT in

    TESTSEL )
        input=${eosdir}/Data13TeV_SingleMuon_2017D/MergedMiniEvents_0_ext0.root
        output=Data13TeV_SingleMuon_2017D.root
        #input=${eosdir}/MC13TeV_GJets_HT100to200_DR04/MergedMiniEvents_0_ext0.root
        #output=MC13TeV_GJets_HT100to200_DR04.root \
        #input=${eosdir}/Data13TeV_SingleMuon_2017C/MergedMiniEvents_0_ext0.root
        #output=Data13TeV_SingleMuon_2017C.root
	python scripts/runLocalAnalysis.py \
            -i ${input} -o ${output} \
            --njobs 1 -q local --debug \
            --era era2017 -m VBFVectorBoson::RunVBFVectorBoson --ch 0 --runSysts;
        ;;
    SEL )
        #--only data/era2017/vbf_samples.json --exactonly \
	python scripts/runLocalAnalysis.py -i ${eosdir} \
            --only MC \
            -o ${outdir} \
            -q ${queue} \
            --era era2017 -m VBFVectorBoson::RunVBFVectorBoson --ch 0 --runSysts;
	;;

    MERGE )
	./scripts/mergeOutputs.py ${outdir};
	;;
    PLOT )
	commonOpts="-i ${outdir} --puNormSF puwgtctr -j data/era2017/vbf_samples.json -l ${fulllumi}  --saveLog --mcUnc ${lumiUnc}"
        commonOpts="${commonOpts} --lumiSpecs VBFA:${vbflumi},HighPtA:${fulllumi},MM:${fulllumi}"
	python scripts/plotter.py ${commonOpts}; 
	;;
    WWW )
	mkdir -p ${wwwdir}/sel
	cp ${outdir}/plots/*.{png,pdf} ${wwwdir}/sel
	cp test/index.php ${wwwdir}/sel
        echo "Check plots in ${wwwdir}/sel"
	;;
esac