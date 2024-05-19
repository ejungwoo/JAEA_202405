#include "Analysis.h"

// Replace define value to "coutx" to omit printing
#define coutd cout<<"+\033[0;36m"<<__LINE__<<" "<<__FILE__<<" #\033[0m "
#define couti cout<<"\033[0;32m==\033[0m "
#define coutw cout<<"\033[0;33mWR\033[0m "
#define coute cout<<"\033[0;31mER\033[0m "
//#define coutt cout<<"\033[0;33mTSERROR\033[0m "
#define coutt coutx

Analysis::Analysis()
{
  coutx.open("out/dummy_stream");
  InitializeMapping();
}

void Analysis::InitializeMapping()
{
  std::ifstream mapFile(fMapFileName);
  if (mapFile.fail()) {
    coute << "Cannot open mapping file: " << fMapFileName << endl;
    return;
  }

  fMapDetectorType = new int*[fNumModules];
  fMapDetectorChannel = new int*[fNumModules];
  fMapDetectorReplaced = new bool*[fNumModules];
  fMapDetectorGroup = new int*[fNumModules];
  fMapDetectorRFMod = new int*[fNumModules];
  fMapDetectorRFMCh = new int*[fNumModules];
  for (int iModule=0; iModule<fNumModules; ++iModule) {
    fMapDetectorType[iModule] = new int[fNumChannels];
    fMapDetectorChannel[iModule] = new int[fNumChannels];
    fMapDetectorReplaced[iModule] = new bool[fNumChannels];
    fMapDetectorGroup[iModule] = new int[fNumChannels];
    fMapDetectorRFMod[iModule] = new int[fNumChannels];
    fMapDetectorRFMCh[iModule] = new int[fNumChannels];
    for (int iChannel=0; iChannel<fNumChannels; ++iChannel) {
      fMapDetectorType[iModule][iChannel] = kDummyDetector;
      fMapDetectorChannel[iModule][iChannel] = -1;
      fMapDetectorReplaced[iModule][iChannel] = false;
      fMapDetectorGroup[iModule][iChannel]  = 0;
      fMapDetectorRFMod[iModule][iChannel] = -1;
      fMapDetectorRFMCh[iModule][iChannel] = -1;
    }
  }
  for (int module=0; module<20; ++module)
    fMapModuleToMIdx[module] = -1;

  TString name;
  string buffer;
  bool replaced;
  UShort_t module0, channelID0, module, channelID, detectorChannel, dES1Group, midx;

  getline(mapFile, buffer); // skip header
  while (mapFile >> module >> midx >> channelID >> name >> detectorChannel >> dES1Group >> replaced)
  {
    int detectorType = kDummyDetector;
    if (name=="S1Junction")   detectorType = kS1Junction;
    if (name=="S1Ohmic")      detectorType = kS1Ohmic;
    if (name=="S3Junction")   detectorType = kS3Junction;
    if (name=="S3Ohmic")      detectorType = kS3Ohmic;
    if (name=="dEDetector")   detectorType = kdEDetector;
    if (name=="Scintillator") detectorType = kScintillator;
    if (name=="FaradayCup")   detectorType = kFaradayCup;
    fMapDetectorType[midx][channelID] = detectorType;
    fMapDetectorChannel[midx][channelID] = detectorChannel;
    fMapDetectorReplaced[midx][channelID] = replaced;
    fMapDetectorGroup[midx][channelID] = dES1Group;
    if (replaced) {
      mapFile >> module0 >> channelID0;
      fMapDetectorRFMod[midx][channelID] = module0;
      fMapDetectorRFMCh[midx][channelID] = channelID0;
    }
    if (fMapModuleToMIdx[module]==-1)
      fMapModuleToMIdx[module] = midx;
  }
  mapFile.close();

  fDetectorName[kDummyDetector] = "X";
  fDetectorName[kS1Junction   ] = "S1J";
  fDetectorName[kS1Ohmic      ] = "S1O";
  fDetectorName[kS3Junction   ] = "S3J";
  fDetectorName[kS3Ohmic      ] = "S3O";
  fDetectorName[kdEDetector   ] = "dE";
  fDetectorName[kScintillator ] = "SC";
  fDetectorName[kFaradayCup   ] = "FC";
}

void Analysis::RunConversion(int runNo, TString pathIn)
{
  if (fEnergyConversionSet==false&&fShowEnergyConversion==true) {
    coutw << "Energy conversion is not set! Cannot proceed energy conversion." << endl;
    fShowEnergyConversion = false;
  }

  fRunNo = runNo; 
  fRunName = Form("[RUN%03d]  ",fRunNo);
  fPathToInput = (pathIn.IsNull() ? TString("/home/daquser/data/LiCD2Irrad/analysis/input/") : pathIn);

  InitializeConversion();
  ConfigureDateTime();
  MakeOutputFile();
  InitializeDrawing();
  ReadDataFile();
  EndOfConversion();
}

void Analysis::AddEnergyCalibration(TString name)
{
  couti << "Set energy calibration functions from " << name << endl;
  for (int midx=0; midx<fNumModules; ++midx)
    for (int iChannel=0; iChannel<fNumChannels; ++iChannel)
      fFxEnergyConversion[midx][iChannel] = nullptr;

  TFile* file = new TFile(name,"read");

  for (int midx=0; midx<fNumModules; ++midx) {
    for (int iChannel=0; iChannel<fNumChannels; ++iChannel) {
      fFxEnergyConversion[midx][iChannel] = (TF1*) file -> Get(Form("AToE_%d_%d",midx,iChannel));
    }
  }

  for (int midx=0; midx<fNumModules; ++midx) {
    bool missing = false;
    for (int iChannel=0; iChannel<fNumChannels; ++iChannel)
      if (fFxEnergyConversion[midx][iChannel]==nullptr) {
        missing = true;
        break;
      }
    if (!missing) break;
    coute << "Missing ECal: ";
    for (int iChannel=0; iChannel<fNumChannels; ++iChannel)
      if (fFxEnergyConversion[midx][iChannel]==nullptr)
        cout << "(" << midx << "," << iChannel << ") ";
    cout << endl;
  }

  fEnergyConversionSet = true;
  //file -> Close();
}

void Analysis::ReadSummaryFile(TString fileName)
{
  fFileSummary = new TFile(fileName,"read");
  if (fFileSummary -> IsZombie()) {
    coute << "File " << fileName << " is zombie!" << endl;
    fFileSummary = nullptr;
    return;
  }
  fTreeSummary = (TTree*) fFileSummary -> Get("event");
  if (fTreeSummary==nullptr) {
    coute << "Tree from " << fileName << " is zombie!" << endl;
    return;
  }
  fTreeSummary -> SetBranchAddress("ts",&bTimeStamp);
  fTreeSummary -> SetBranchAddress("nch",&bNumChannels);
  fTreeSummary -> SetBranchAddress("de",&bdE);
  fTreeSummary -> SetBranchAddress("ee",&bdES1);
  fTreeSummary -> SetBranchAddress("ch",&fChannelArray);
  fNumEventsSummary = fTreeSummary -> GetEntries();

  //fFileSummary -> ls();
  //fRunNo = ((TParameter<int>*)fFileSummary->Get("run"))->GetVal();
  fRunNo = TString(((TNamed*) fFileSummary->Get("run"))->GetTitle()).Atoi();
  couti << fileName << "(" << fRunNo << ") containing " << fNumEventsSummary << " events" << endl;
}

Double_t Analysis::FxTwoAlpha(Double_t *xy, Double_t *par)
{
  // fAlphaEnergy1 = 5.486 MeV (85.2 %)
  // fAlphaEnergy2 = 5.443 MeV (12.8 %)

  double x = xy[0];

  double ADCOffset = par[0];
  double energyResolution = par[1];

  double mean1 = par[2]; // ADC mean of 5.486 MeV peak
  double meanP1 = mean1 - ADCOffset; // pure ADC without ADC-offset
  double sigma1 = energyResolution * meanP1;
  double amplitude1 = par[3];

  double ADCEnergyRatio = meanP1 / fAlphaEnergy1;

  double mean2 = fAlphaEnergy2 * ADCEnergyRatio + ADCOffset;
  double meanP2 = mean2 - ADCOffset; // pure ADC without ADC-offset
  double sigma2 = energyResolution * meanP2;
  double amplitude2 = amplitude1 * sigma1 / fAEBR / sigma2;

  double value1 = amplitude1*exp(-0.5*((x-mean1)*(x-mean1)/sigma1/sigma1));
  double value2 = amplitude2*exp(-0.5*((x-mean2)*(x-mean2)/sigma2/sigma2));
  double value  = value1 + value2;

  return value;
}

void Analysis::Convert2APParameters(double* par, double &mean1, double &sigma1, double &amplitude1, double &mean2, double &sigma2, double &amplitude2)
{
  double ADCOffset = par[0];
  double energyResolution = par[1];

  mean1 = par[2]; // ADC mean of 5.486 MeV peak
  double meanP1 = mean1 - ADCOffset; // pure ADC without ADC-offset
  sigma1 = energyResolution * meanP1;
  amplitude1 = par[3];

  double ADCEnergyRatio = meanP1 / fAlphaEnergy1;

  mean2 = fAlphaEnergy2 * ADCEnergyRatio + ADCOffset;
  double meanP2 = mean2 - ADCOffset; // pure ADC without ADC-offset
  sigma2 = energyResolution * meanP2;
  amplitude2 = amplitude1 * sigma1 / 6.65625 / sigma2; // 85.2 / 12.8 = 6.65625; 
}

TF1* Analysis::CreateFxTwoAlpha(TString name, double min, double max)
{
  auto f1 = new TF1(name,this,&Analysis::FxTwoAlpha,min,max,4,"Analysis","FxTwoAlpha");
  return f1;
}

void Analysis::InitializeAlphaAnalysis(TString fileName)
{
  if (fFileAlpha!=nullptr)
    return;
  if (fileName.IsNull())
    fileName = Form("RUN%03d.alpha.root",fRunNo);
  if (fFileNameAlpha.IsNull())
    fFileNameAlpha = fPathToOutput+fileName;
  cout << endl;
  couti << "Creating output file " << fFileNameAlpha << endl;
  fFileAlpha = new TFile(fFileNameAlpha,"recreate");
  WriteRunParameters(fFileAlpha,1);
}

void Analysis::AnalyzeAlphaTestModule(int midx, bool drawAnalysis, TString fileName)
{
  InitializeAlphaAnalysis(fileName);
  TCanvas* cvs = nullptr;
  if (drawAnalysis) {
    cvs = new TCanvas(Form("cvsEMIdx%d",midx),Form("M%d",midx),1600,900);
    cvs -> Divide(4,4);
  }
  bool moduleDataExist = false;
  for (auto channelID=0; channelID<fNumChannels; ++channelID)
  {
    TVirtualPad* pad = nullptr;
    if (drawAnalysis)
      pad = cvs->cd(channelID+1);

    bool channelDataExist = AnalyzeAlphaTest(midx, channelID, drawAnalysis, pad);
    moduleDataExist = (moduleDataExist || channelDataExist);
  }

  if (drawAnalysis)
  {
    if (!moduleDataExist) {
      coute << "Module " << midx << " is empty!" << endl;
        delete cvs;
    }
    else if (fFileAlpha!=nullptr) {
      fFileAlpha -> cd();
      cvs -> Write();
    }
  }

  cout << endl;
  if (fFileAlpha!=nullptr)
    couti << fFileAlpha -> GetName() << endl;
}

bool Analysis::AnalyzeAlphaTest(int midx, int channelID, bool drawAnalysis, TVirtualPad* cvs)
{
  if (fTreeSummary==nullptr) {
    coute << "Summary tree is nullptr! Run ReadSummaryFile before this method!";
    return false;
  }
  
  bool isSingleDrawing = false;
  if (cvs==nullptr)
    isSingleDrawing = true;

  couti << Form("channel (%d,%d)",midx,channelID) << endl;

  auto gid = GetGlobalID(midx, channelID);
  if (fFitAlpha==nullptr)
  {
    fFitAlpha = CreateFxTwoAlpha("fxTwoAlpha",0,fMaxADC);
    fFitAlpha1 = new TF1("fxAlpha1","gaus(0)",0,fMaxADC);
    fFitAlpha2 = new TF1("fxAlpha2","gaus(0)",0,fMaxADC);
    fFitAlpha1 -> SetLineColor(kBlue);
    fFitAlpha2 -> SetLineColor(kGreen+1);
  }
  double mean1, sigma1, amplitude1, mean2, sigma2, amplitude2;

  TString nameHist = Form("histE%d",gid);
  TString titleHist = Form("channel (%d,%d) energy distribution;ADC;count",midx,channelID);
  auto histE = new TH1D(nameHist,titleHist,fNumADC,0,fMaxADC);
  fTreeSummary -> Project(nameHist, "ch.adc", Form("ch.gid==%d",gid));
  if (histE->GetEntries()==0) {
    coute << "Data is empty!" << endl;
    //0.001810*(x-0.0000)
    if (fFileAlpha!=nullptr) {
      auto fxADCToEnergyDummy = new TF1(Form("AToE_%d_%d",midx,channelID),"0.001810*(x-0.0000)",0,10000);
      fxADCToEnergyDummy -> Write();
    }
    return false;
  }

  double mean = histE -> GetMean();
  double sig = histE -> GetStdDev();

  double bin = histE->GetMaximumBin();
  double yMax = histE->GetBinContent(bin);
  double xPeak = histE->GetBinCenter(bin);

  auto x1 = xPeak - 80;
  auto x2 = xPeak + 80;
  fFitAlpha -> SetParameters(10,0.005,xPeak,yMax);
  fFitAlpha -> SetParLimits(0,0,100);
  histE -> Fit(fFitAlpha,"Q0N","",x1,x2);
  auto parameters = fFitAlpha -> GetParameters();
  Convert2APParameters(parameters, mean1, sigma1, amplitude1, mean2, sigma2, amplitude2);
  double ADCOffset = parameters[0];
  if (ADCOffset<0.001)
    ADCOffset = 0;
  double ADCEnergyRatio = (mean1 - ADCOffset) / fAlphaEnergy1;
  double energyADCRatio = fAlphaEnergy1 / (mean1-ADCOffset);
  double energyResolution = parameters[1] * 2.354;
  double energyResolutionKeV = energyResolution * 1000*fAlphaEnergy1;
  cout << "   Energy res. : " << 100*energyResolution << " % (" << Form("%.2f KeV)",energyResolutionKeV) << endl;
  cout << "   Energy/ADC. : " << energyADCRatio << endl;
  cout << "   ADC offset  : " << ADCOffset << endl;
  cout << Form("   AMS(%.3f)",fAlphaEnergy1) <<" : " << amplitude1 << ", " << mean1 << ", " << sigma1 << endl;
  cout << Form("   AMS(%.3f)",fAlphaEnergy2) <<" : " << amplitude2 << ", " << mean2 << ", " << sigma2 << endl;
  fFitAlpha1 -> SetParameters(amplitude1, mean1, sigma1);
  fFitAlpha2 -> SetParameters(amplitude2, mean2, sigma2);

  if (fFileAlpha!=nullptr) {
    auto fxADCToEnergy = new TF1(Form("AToE_%d_%d",midx,channelID),Form("%f*(x-%.4f)",energyADCRatio,ADCOffset),0,10000);
    fxADCToEnergy -> Write();
  }

  if (drawAnalysis)
  {
    TString nameCvs = Form("cvsE%d",gid);
    if (cvs==nullptr)
      cvs = new TCanvas(nameCvs,"",800,450);

    if (isSingleDrawing) SetAttribute(histE,cvs,1);
    else SetAttribute(histE,cvs,16);

    histE -> GetXaxis() -> SetRangeUser(mean2-8*sigma2,mean1+8*sigma1);
    histE -> Draw();
    fFitAlpha -> DrawCopy("samel");
    fFitAlpha1 -> DrawCopy("samel");
    fFitAlpha2 -> DrawCopy("samel");

    const char* percent = "%";
    if (1)
    {
      auto legend = new TLegend(0.18,0.30,0.47,0.85);
      legend -> SetBorderSize(0);
      legend -> SetFillStyle(0);
      legend -> SetTextSize(0.07);
      legend -> AddEntry("",Form("ER = %0.2f %s", 100*energyResolution, percent),"");
      legend -> AddEntry("",Form("(%0.2f KeV)", energyResolutionKeV),"");
      if (ADCOffset>0.001) legend -> AddEntry("",Form("ADC offset = %.3f", ADCOffset),"");
      legend -> AddEntry(fFitAlpha1,Form("1) %0.2f", mean1),"l");
      legend -> AddEntry(fFitAlpha2,Form("2) %0.2f", mean2),"l");
      legend -> Draw();
    }
    else
    {
      auto legend = new TLegend(0.15,0.40,0.47,0.85);
      legend -> SetBorderSize(0);
      legend -> SetFillStyle(0);
      legend -> SetTextSize(0.05);
      legend -> AddEntry("",Form("Resolution = %0.2f %s", 100*energyResolution, percent),"");
      if (ADCOffset>0.001) legend -> AddEntry("",Form("ADC offset = %.3f", ADCOffset),"");
      legend -> AddEntry(fFitAlpha1,Form("M(%.3f) = %0.2f", fAlphaEnergy1,mean1),"l");
      legend -> AddEntry(fFitAlpha2,Form("M(%.3f) = %0.2f", fAlphaEnergy2,mean2),"l");
      legend -> Draw();
    }

    if (isSingleDrawing && fFileAlpha!=nullptr) {
      fFileAlpha -> cd();
      cvs -> Write();
    }
  }

  return true;
}

void Analysis::InitializeConversion()
{
  fPathToOutput = "/home/daquser/data/LiCD2Irrad/analysis/out/";
  fCountEvents = 0;
  fChannelArray = nullptr;
  fCountChannels = 0;
  fCountAllChannels = 0;
  fCountTSError = 0;
  fReturnIfNoFile = true;
  fCoincidenceCount[0] = 0;
  fCoincidenceCount[1] = 0;
  fCoincidenceCount[2] = 0;
  fCoincidenceCount[3] = 0;
  fCoincidenceCount[4] = 0;

  if (fSkipTSError )
  {
    coutw << "The program will ignore time-stamp error!" << endl;
    coutw << "The program will ignore time-stamp error!" << endl;
    coutw << "The program will ignore time-stamp error!" << endl;
    coutw << "The program will ignore time-stamp error!" << endl;
  }
}

void Analysis::InitializeDrawing()
{
  if (fFileOut!=nullptr)
    fFileOut -> cd();

  if (fUpdateDrawingEveryNEvent>0)
  {
    fCvsOnline2 = new TCanvas("cvsTS",fRunName+" online update canvas 2",1600,900);
    fCvsOnline2 -> Divide(2,2);
    fPadTrgRate = fCvsOnline2 -> cd(1);
    fPadEvtRate = fCvsOnline2 -> cd(2);
    fPadTSDist1 = fCvsOnline2 -> cd(3);
    fPadTSDist2 = fCvsOnline2 -> cd(4);

    fCvsOnline1 = new TCanvas("cvsOnline",fRunName+" online update canvas 1",1600,900);
    fCvsOnline1 -> Divide(2,2);
    fPadChCount = fCvsOnline1 -> cd(1);
    fPadADC     = fCvsOnline1 -> cd(2);
    fPadEVSCh   = fCvsOnline1 -> cd(3);
    fPaddEVSE   = fCvsOnline1 -> cd(4);
  }

  fHistTriggerRate      = new TH1D("histTrgRateG",fRunName+"trigger rate;trigger/s;count",fNumRate,0,fMaxRate);
  fHistTriggerRateError = new TH1D("histTrgRateE",fRunName+"Bad trigger rate;trigger/s;count",fNumRate,0,fMaxRate);
  fHistTriggerRateError -> SetLineStyle(2);
  fHistTriggerRateError -> SetLineColor(kRed);

  fHistEventRate      = new TH1D("histEvtRateG",fRunName+"event rate;event/s;count",fNumRate,0,fMaxRate);
  fHistEventRateError = new TH1D("histEvtRateE",fRunName+"Bad event rate;event/s;count",fNumRate,0,fMaxRate);
  fHistEventRateError -> SetLineStyle(2);
  fHistEventRateError -> SetLineColor(kRed);

  fHistTSDist1 = new TH1D("histTSDist1",fRunName+" TS distance (+);TS-dist;count", 20,0,20);
  fHistTSDist2 = new TH1D("histTSDist2",fRunName+" TS distance (-);TS-dist;count", 50,-1000000,0);

  fHistChCount = new TH1D("histChCount",fRunName+" channel count;global-ch;event count", fNumCh,0,fNumCh);
  fHistChCount -> SetFillColor(29);

  fHistADC = new TH1D("histADC",fRunName+" ADC (all channels);ADC",fNumADC,0,fMaxADC);
  fHistADC -> SetFillColor(29);
  fHistE = new TH1D("histEnergy",fRunName+" energy (all channels);energy (MeV)",fNumE,0,fMaxE);
  fHistE -> SetFillColor(29);

  fHistAVSCh = new TH2D("histAVSCh", fRunName+" ADC vs channel-id;global-ch;ADC", fNumCh,0,fNumCh,fNumADC,0,fMaxADC);
  fHistEVSCh = new TH2D("histEVSCh", fRunName+" energy vs channel-id;global-ch;energy (MeV)", fNumCh,0,fNumCh,fNumE,0,fMaxE);
  fHistdAVSA = new TH2D("histdAVSA", fRunName+" dA vs dA + S1;dA + S1A;dA", fNumADC, 0, 2*fMaxADC, fNumADC, 0, fMaxADC);
  fHistdEVSE = new TH2D("histdEVSE", fRunName+" dE vs dE + S1;dE + S1E (MeV);dE (MeV)", fNumE,0,fMaxE+fMaxdE, fNumdE, 0, fMaxdE);

  if (fUpdateDrawingEveryNEvent>0) {
    SetAttribute(fHistTriggerRate,fPadTrgRate,1);
    SetAttribute(fHistTriggerRateError,fPadTrgRate,1);
    SetAttribute(fHistEventRate,fPadEvtRate,1);
    SetAttribute(fHistEventRateError,fPadEvtRate,1);
    SetAttribute(fHistTSDist1,fPadTSDist1,1);
    SetAttribute(fHistTSDist2,fPadTSDist2,1);
    SetAttribute(fHistChCount,fPadChCount);
    SetAttribute(fHistADC,fPadADC);
    SetAttribute(fHistE,fPadADC);
    SetAttribute(fHistAVSCh,fPadEVSCh,1,true);
    SetAttribute(fHistEVSCh,fPadEVSCh,1,true);
    SetAttribute(fHistdAVSA,fPaddEVSE,1,true);
    SetAttribute(fHistdEVSE,fPaddEVSE,1,true);
  }

  UpdateCvsOnline(true);
}

void Analysis::SetAttribute(TH1* hist, TVirtualPad* pad, int npad, bool is2D)
{
  auto ax = hist -> GetXaxis();
  auto ay = hist -> GetYaxis();

  if (npad==2)
  {
    if (pad!=nullptr) {
      pad -> SetLeftMargin(0.15);
      pad -> SetRightMargin(0.10);
      pad -> SetBottomMargin(0.12);
      pad -> SetTopMargin(0.12);
    }
    gStyle -> SetTitleH(0.07);
    ax -> SetLabelSize(0.04);
    ay -> SetLabelSize(0.04);
    ax -> SetTitleSize(0.05);
    ay -> SetTitleSize(0.05);
    ax -> SetTitleOffset(1.0);
    ay -> SetTitleOffset(1.50);
  }
  else if (npad>=16)
  {
    if (pad!=nullptr) {
      pad -> SetLeftMargin(0.12);
      pad -> SetRightMargin(0.05);
      pad -> SetBottomMargin(0.12);
      pad -> SetTopMargin(0.12);
    }
    gStyle -> SetTitleH(0.08);
    ax -> SetLabelSize(0.06);
    ay -> SetLabelSize(0.06);
    ax -> SetTitleSize(0.07);
    ay -> SetTitleSize(0.07);
    ax -> SetTitleOffset(0.8);
    ay -> SetTitleOffset(0.82);
  }
  else
  {
    if (pad!=nullptr) {
      pad -> SetLeftMargin(0.11);
      pad -> SetRightMargin(0.05);
      if (is2D)
        pad -> SetRightMargin(1.2);
      pad -> SetBottomMargin(0.13);
      pad -> SetTopMargin(0.11);
    }
    gStyle -> SetTitleH(0.06);
    ax -> SetLabelSize(0.04);
    ay -> SetLabelSize(0.04);
    ax -> SetTitleSize(0.05);
    ay -> SetTitleSize(0.05);
    ax -> SetTitleOffset(1.1);
    ay -> SetTitleOffset(1.0);
  }
}

void Analysis::ConfigureDateTime()
{
  cout << endl;
  couti << Form("Looking for RUN%03d in %s",fRunNo,fPathToInput.Data()) << " ..." << endl;

  TList *listOfFiles = TSystemDirectory(fPathToInput,fPathToInput).GetListOfFiles();
  TIter next(listOfFiles);
  TSystemFile* fileObject;
  while ((fileObject=(TSystemFile*)next()))
  {
    int idx = 0;
    if (fileObject->IsDirectory()) continue;
    TString fileName = fileObject -> GetName();
    if (fileName.Index(Form("RUN%03d",fRunNo))!=0) continue;
    if (fileName.EndsWith(".dat")==false) continue;
    fDateTime = fileName(7,12);
    int fileNumber = TString(fileName(25,3)).Atoi();
    if (fileNumber>fFileNumberMax)
      fFileNumberMax = fileNumber;
    couti << fileName << endl;
  }
  if (fFileNumberMax>fFileNumberRange2)
    fFileNumberMax=fFileNumberRange2;
}

void Analysis::WriteRunParameters(TFile* file, int option)
{
  file -> cd();
  (new TNamed("run", Form("%d",fRunNo))) -> Write();
  if (option==0) {
    (new TNamed("Energy threshold   :", Form("%d",fADCThreshold))) -> Write();
    (new TNamed("Draw every n-event :", Form("%d",fUpdateDrawingEveryNEvent))) -> Write();
    (new TNamed("Return if no file  :", Form("%d",fReturnIfNoFile))) -> Write();
    (new TNamed("Ignore file update :", Form("%d",fIgnoreFileUpdate))) -> Write();
    (new TNamed("Ignore TS decrease :", Form("%d",fSkipTSError))) -> Write();
    (new TNamed("Event count limit  :", Form("%d",fEventCountLimit))) -> Write();
    (new TNamed("File number range  :", Form("%d %d",fFileNumberRange1,fFileNumberRange2))) -> Write();
    (new TNamed("ADC Threshold      :", Form("%d",fADCThreshold))) -> Write();
    (new TNamed("Coincidence dTS cut:", Form("%d",fCoincidenceTSRange))) -> Write();
  }
}

void Analysis::MakeOutputFile()
{
  if (fFileNameOut.IsNull())
    fFileNameOut = fPathToOutput+Form("RUN%03d.summary.root",fRunNo);
  cout << endl;
  couti << "Creating output file " << fFileNameOut << endl;
  fFileOut = new TFile(fFileNameOut,"recreate");
  fTreeOut = new TTree("event","");
  fChannelArray = new TClonesArray("ChannelData",10);
  fTreeOut -> Branch("ts",&bTimeStamp);
  fTreeOut -> Branch("nch",&bNumChannels);
  fTreeOut -> Branch("de",&bdE);
  fTreeOut -> Branch("ee",&bdES1);
  fTreeOut -> Branch("ch",&fChannelArray);

  WriteRunParameters(fFileOut,0);
}

void Analysis::ReadDataFile()
{
  ifstream fileIn;
  streamsize fileSize;
  streamsize fileSizeOld = 0; // Size of opened Raw-Data file
  const streamsize fileSizeMax = 500000000; // 500 MB
  UShort_t countInputs = fFileNumberRange1; // Number of Files read
  UShort_t module, channelID, adc;
  Long64_t timeStampLocal, timeStamp;
  Int_t tsGroup;
  Int_t numData = 0;
  char buffer[256];
  int countOpenFileTrials = 0;

  ChannelData* channelData = NULL;
  fTimeStampPrev = -1;

  while(1)
  {
    if (fFileNumberMax >=0 && countInputs>fFileNumberMax) {
      couti << "Number of inputs " << countInputs << " exceed maximum input number " << fFileNumberMax << endl;
      break;
    }

    TString fileNameInput = TString::Format("%s/RUN%03d_%s_list_%03d.dat", fPathToInput.Data(),fRunNo,fDateTime.Data(),countInputs);
    cout << endl;
    couti << "Reading " << fileNameInput << endl;

    countOpenFileTrials = 0;
    while (1) // endless loop (2) to check the status of the Raw-Data file //
    {
      fileIn.open(fileNameInput);
      if (fileIn.fail())
      {
        fileIn.close();
        if (countOpenFileTrials>10) {
          coute << "Failed to open file!" << endl;
          break;
        }
        else {
          countOpenFileTrials++;
          coutw << "There is no file!" << endl;
          if (fReturnIfNoFile)
            break;
          couti << "waiting(" << countOpenFileTrials << ") 3s ..." << endl;
          sleep(3);
          continue;
        }
      }

      fileSize = fileIn.seekg(0, ios::end).tellg(); // obtain the size of file
      fileIn.seekg(0, ios::beg); // rewind

      if (fileSize>fileSizeMax || (countOpenFileTrials!=0 && fileSize==fileSizeOld)) // good file or final file
      {
        countOpenFileTrials = 0;
        countInputs++;
        break;
      }
      else if(countOpenFileTrials==0){ // first try
        countOpenFileTrials++;
        fileIn.close();
        fileSizeOld = fileSize;
        if (fFirstFileOpened) {
          couti << "waiting(" << countOpenFileTrials << ") 3s ..." << endl;
          sleep(3);
        }
        fFirstFileOpened = true;
      }
      else if(fileSize > fileSizeOld){ // writing the file is still continued ...
        if (fIgnoreFileUpdate)
          break;
        fileIn.close();
        fileSizeOld = fileSize;
        couti << "waiting(" << countOpenFileTrials << ") 60s ..." << endl;
        sleep(60);
      }
    }

    if (countOpenFileTrials != 0) {
      coutw << fileNameInput << " is not found! exit." << endl;
      break;
    }

    //couti << "File size is " << fileSize << endl;

    fChannelArray -> Clear("C");
    Long64_t countEventsSingleFile = 0;
    fCountChannels = 0;
    Long64_t countLine = 0;
    fdEArrayIdx.clear();
    fS1ArrayIdx.clear();
    fdEADC = 0.;
    fS1ADC = 0.;
    while (fileIn >> buffer)
    {
      countLine++;
      numData = (Int_t) sscanf(buffer, "%hu,%hu,%hu,%lld,%hu", &module, &channelID, &adc, &timeStampLocal, &tsGroup);
      if (numData != 5) {
        coute << TString::Format("Number of data in line is not 5 (%d) %u %u %u %lld %u", numData, module, channelID, adc, timeStampLocal, tsGroup) << endl;
        continue;
      }

      timeStamp = timeStampLocal;
      if (tsGroup>0)
        timeStamp += (Long64_t)(tsGroup) * 0xFFFFFFFFFF; 

      if (bTimeStamp>=0) {
        int dts = int(timeStamp)- int(fTimeStampPrevTrue);
        if (dts>=0) fHistTSDist1 -> Fill(dts);
        else fHistTSDist2 -> Fill(dts);
      }

      int eventStatus = kNextEvent;
      if (!fSkipTSError)
      {
        if (timeStamp-fTimeStampPrev<=fCoincidenceTSRange) eventStatus = kSameEvent;
        else if (timeStamp<fTimeStampPrev) eventStatus = kNextEvent;
        else if (timeStamp>fTimeStampPrev) eventStatus = kNextEvent;
      }
      else
      {
        if (timeStamp-fTimeStampPrev<=fCoincidenceTSRange) eventStatus = kSameEvent;
        else if (timeStamp<fTimeStampPrev) eventStatus = kNextEvent;
        else if (timeStamp>fTimeStampPrev) eventStatus = kTSError;
      }

      int timeStatus = kSameSecond;
      double dSecond = (timeStamp-fTimeStampLastSec)*fSecondPerTS;
      if (dSecond<0) timeStatus = kTimeError;
      else if (dSecond>1) timeStatus = fNextSecond;
      else timeStatus = kSameSecond;

      if (timeStatus==kSameSecond) {
        fCountTriggerPerSec++;
      }
      else if (timeStatus==kTimeError) {
        fCountTriggerPerSecError++;
      }
      else if (timeStatus==fNextSecond)
      {
        fHistTriggerRate -> Fill(fCountTriggerPerSec);
        fHistTriggerRateError -> Fill(fCountTriggerPerSecError);
        fCountTriggerPerSecError = 0;
        fCountTriggerPerSec = 0;
        fCountTriggerPerSec++;
        fTimeStampLastSec = timeStamp;
        fTimeStampLastSecError = timeStamp;
      }

      if (CheckDataLineCondition(adc,eventStatus,timeStamp)==false)
        continue;
      if (fExitAnalysis)
        break;

      if (eventStatus==kSameEvent)
      {
        channelData = (ChannelData*) fChannelArray -> ConstructedAt(fCountChannels++);
      }
      else if (eventStatus==kNextEvent)
      {
        if (countEventsSingleFile>0) {
          FillDataTree();
          AskUpdateDrawing();
        }
        if (fExitAnalysis)
          break;

        fCountChannels = 0;
        fChannelArray -> Clear("C");
        channelData = (ChannelData*) fChannelArray -> ConstructedAt(fCountChannels++);

        fTimeStampPrev = timeStamp;
        fTimeStampPrevTrue = timeStamp;
        fCountEvents++;
        countEventsSingleFile++;

        if (timeStatus==kSameSecond) {
          fCountEventsPerSec++;
        }
        else if (timeStatus==kTimeError) {
          fCountEventsPerSecError++;
        }
        else if (timeStatus==fNextSecond)
        {
          fHistEventRate -> Fill(fCountEventsPerSec);
          fHistEventRateError -> Fill(fCountEventsPerSecError);
          fCountEventsPerSecError = 0;
          fCountEventsPerSec = 0;
          fCountEventsPerSec++;
        }
      }

      Short_t midx = GetModuleIndex(module);
      int gid = GetGlobalID(midx, channelID);
      double energy = 0;
      if (fEnergyConversionSet)
          energy = GetCalibratedEnergy(midx,channelID,adc);
      channelData -> SetData(midx,channelID,gid,adc,energy,timeStampLocal,tsGroup,timeStamp);
      fCountAllChannels++;

      if (fMapDetectorType[midx][channelID]==kS1Junction) {
        fS1ArrayIdx.push_back(fCountChannels-1);
        fS1ADC = adc;
      }
      if (fMapDetectorType[midx][channelID]==kdEDetector) {
        fdEArrayIdx.push_back(fCountChannels-1);
        fS1ADC = adc;
      }

      if (fUpdateDrawingEveryNEvent>=0)
      {
        fHistChCount -> Fill(gid);
        fHistADC -> Fill(adc);
        fHistAVSCh -> Fill(gid, adc);
        if (fEnergyConversionSet) {
          fHistEVSCh -> Fill(gid, energy);
          fHistE -> Fill(energy);
        }
      }

      if (fEventCountLimit>0 && fCountEvents>=fEventCountLimit) {
        couti << "Event count limit at " << fCountEvents << "!" << endl;
        fExitAnalysis = true;
      }
    }

    if (!fExitAnalysis && fCountEvents>0) 
      FillDataTree();

    if (fExitAnalysis)
      break;

    couti << "Number of events from last file: " << countEventsSingleFile << endl;

    PrintConversionSummary();

    fileIn.close();
  }

  UpdateCvsOnline();
}

bool Analysis::CheckDataLineCondition(double adc, int eventStatus, Long64_t timeStamp)
{
  if (adc < fADCThreshold)
    return false;

  if (eventStatus==kTSError) // time-stamp decreased
  {
    if (timeStamp<fTimeStampPrevTrue) {
      coutt << "Time-stamp error (" << fCountTSError << ") " << fTimeStampPrev << " -> " << timeStamp << endl;
      fTimeStampPrevTrue = timeStamp;
      ++fCountTSError;
    }
    if (fStopAtTSError) {
      coute << "Exit due to Time-stamp error (" << fCountTSError << ") " << fTimeStampPrev << " -> " << timeStamp << endl;
      fExitAnalysis = true;
    }
    return false;
  }

  return true;
}

bool Analysis::CheckEventCondition()
{
  if (fCoincidenceMultCut>0)
    if (fCountChannels!=fCoincidenceMultCut)
      return false;
  return true;
}

bool Analysis::FillDataTree()
{
  if      (fCountChannels==0) fCoincidenceCount[0]++;
  else if (fCountChannels==1) fCoincidenceCount[1]++;
  else if (fCountChannels==2) fCoincidenceCount[2]++;
  else if (fCountChannels==3) fCoincidenceCount[3]++;
  else if (fCountChannels>=4) fCoincidenceCount[4]++;

  if (1) // if there is coincidence between S1 and dE-detector
  {
    bdE = -1;
    bdES1 = -1;
    if (fdEArrayIdx.size()==1 && fS1ArrayIdx.size()==1)
    {
      auto chdE = (ChannelData*) fChannelArray -> At(fdEArrayIdx[0]);
      auto chS1 = (ChannelData*) fChannelArray -> At(fS1ArrayIdx[0]);
      int groupdE = fMapDetectorGroup[chdE->midx][chdE->id];
      int groupS1 = fMapDetectorGroup[chS1->midx][chS1->id];
      bdE = chdE->energy;
      bdES1 = chdE->energy + chS1->energy;
      if (groupdE==groupS1) {
        fHistdAVSA -> Fill(chdE->adc,    chdE->adc    + chS1->adc);
        fHistdEVSE -> Fill(chdE->energy, chdE->energy + chS1->energy);
      }
    }
    fdEArrayIdx.clear();
    fS1ArrayIdx.clear();
    fdEADC = 0.;
    fS1ADC = 0.;
  }

  if (CheckEventCondition()==false)
    return false;

  bTimeStamp = fTimeStampPrev;
  bNumChannels = fCountChannels;
  fTreeOut -> Fill();

  return true;
}

void Analysis::AskUpdateDrawing()
{
  if (fUpdateDrawingEveryNEvent>=0)
  {
    fCountEventsForUpdate++;

    // fUpdateDrawingEveryNEvent==0 will read all events and draw
    if (fUpdateDrawingEveryNEvent!=0 && fCountEventsForUpdate>=fUpdateDrawingEveryNEvent)
    {
      UpdateCvsOnline();
      fCountEventsForUpdate = 0;
      std::string userInput0;
      if (fAutoUpdateDrawing)
        userInput0 = "";
      else {
        cout << "\033[0;32m" << Form("== (%d) Enter / stop / auto / all: ",fCountEvents,fUpdateDrawingEveryNEvent) << "\033[0m";
        std::getline(std::cin, userInput0);
      }
      TString userInput = userInput0;
      userInput.ToLower();
      if (userInput=="stop" || userInput=="exit" || userInput=="x")
        fExitAnalysis = true;
      else if (userInput.Index(".q")==0 || userInput=="q") {
        fExitAnalysis = true;
        fExitRoot = true;
      }
      else
      {
        if (userInput=="auto")
        {
          fAutoUpdateDrawing = true;
          couti << Form("automatically update every %d events",fUpdateDrawingEveryNEvent) << endl;
        }
        if (userInput=="all")
        {
          fUpdateDrawingEveryNEvent = 0;
          couti << "Reading all events" << endl;
        }
        if (userInput.IsDec() && userInput.Atoi()>0)
        {
          fUpdateDrawingEveryNEvent = userInput.Atoi();
          couti << Form("Reading next %d events",fUpdateDrawingEveryNEvent) << endl;
        }
      }
    }
  }
}

void Analysis::UpdateCvsOnline(bool firstDraw)
{
  if (fUpdateDrawingEveryNEvent<=0)
    return;

  if (fCvsOnline1==nullptr)
    return;

  auto DrawModuleBoundary = [this](double yMax) {
    for (int iModule=0; iModule<fNumModules; ++iModule) {
      if (iModule!=0) {
        auto line = new TLine(iModule*fNumChannels,0,iModule*fNumChannels,yMax);
        line -> SetLineColor(kBlack);
        line -> Draw("samel");
      }
      for (int iDiv : {1,2,3}) {
        auto line = new TLine(iModule*fNumChannels+iDiv*4,0,iModule*fNumChannels+iDiv*4,yMax*0.5);
        line -> SetLineColor(kGray+1);
        line -> SetLineStyle(2);
        line -> Draw("samel");
      }
      auto tt = new TText((iModule+0.5)*fNumChannels,yMax*0.1,fDetectorName[fMapDetectorType[iModule][0]]);
      //tt -> SetTextColor(kGray);
      tt -> SetTextAlign(22);
      tt -> Draw("same");
    }
  };

  auto TakeCareOfStatsBox = [this](TH1* hist) {
    TPaveStats* box = dynamic_cast<TPaveStats*>(hist -> FindObject("stats"));
    if (box!=nullptr) {
      box -> Draw();
      //box -> SetBorderSize(0);
    }
  };

  fPadTrgRate -> cd();
  fHistTriggerRate -> Draw();
  fHistTriggerRateError -> Draw("same");
  fPadEvtRate -> cd();
  fHistEventRate -> Draw();
  fHistEventRateError -> Draw("same");

  fPadTSDist1 -> cd();
  fHistTSDist1 -> Draw();
  fPadTSDist2 -> cd();
  fHistTSDist2 -> Draw();

  //fCvsOnline1 -> cd(1);
  fPadChCount -> cd();
  fHistChCount -> Draw();
  DrawModuleBoundary(fHistChCount->GetMaximum()*1.05);
  TakeCareOfStatsBox(fHistChCount);

  //fCvsOnline1 -> cd(2);
  fPadADC -> cd();
  TH1D* histEOnline = fHistADC;
  if (fShowEnergyConversion)
    histEOnline = fHistE;
  histEOnline -> Draw();

  //fCvsOnline1 -> cd(3);
  fPadEVSCh -> cd();
  TH2D* hist2DOnline = fHistAVSCh;
  double yMax = fMaxADC;
  if (fShowEnergyConversion) {
    hist2DOnline = fHistEVSCh;
    yMax = fMaxE;
  }
  hist2DOnline -> Draw("colz");
  DrawModuleBoundary(yMax);
  TakeCareOfStatsBox(hist2DOnline);

  //fCvsOnline1 -> cd(4);
  fPaddEVSE -> cd();
  TH2D* histdEEOnline = fHistdAVSA;
  if (fShowEnergyConversion) {
    histdEEOnline = fHistdEVSE;
  }
  histdEEOnline -> Draw("colz");

  fCvsOnline2 -> Modified();
  fCvsOnline2 -> Update();
  fCvsOnline1 -> Modified();
  fCvsOnline1 -> Update();
}

void Analysis::EndOfConversion()
{
  fFileOut -> cd();
  couti << "Writting tree ..." << endl;
  fTreeOut -> Write();

  if (fCvsOnline1!=nullptr)
  {
    UpdateCvsOnline();
    fCvsOnline1 -> Write();
  }
  fHistChCount -> Write();
  fHistADC -> Write();
  fHistAVSCh -> Write();
  fHistEVSCh -> Write();

  PrintConversionSummary();

  couti << "End of conversion!" << endl;
  couti << "Output file name: " << fFileNameOut << endl;

  if (fExitRoot)
    gApplication -> Terminate();
}

void Analysis::PrintConversionSummary()
{
  cout << endl;
  couti << "Number of events: " << fCountEvents << endl;
  couti << "Number of all channels: " << fCountAllChannels << endl;
  couti << "Number of times TS-error occured: " << fCountTSError << endl;
  couti << "Number of events with 0   coincidence channels: " << fCoincidenceCount[0] << endl;
  couti << "Number of events with 1   coincidence channels: " << fCoincidenceCount[1] << endl;
  couti << "Number of events with 2   coincidence channels: " << fCoincidenceCount[2] << endl;
  couti << "Number of events with 3   coincidence channels: " << fCoincidenceCount[3] << endl;
  couti << "Number of events with =>4 coincidence channels: " << fCoincidenceCount[4] << endl;
}

int Analysis::GetModuleIndex(int module)
{
  return fMapModuleToMIdx[module];
}

int Analysis::GetGlobalID(UShort_t midx, UShort_t channelID)
{
  int globalID = midx*fNumChannels + channelID;
  return globalID;
}

void Analysis::GetModCh(int globalID, UShort_t &midx, UShort_t &channelID)
{
  midx = globalID / fNumChannels;
  channelID = globalID % fNumChannels;
}

double Analysis::GetCalibratedEnergy(int midx, int channelID, int adc)
{
  return fFxEnergyConversion[midx][channelID] -> Eval(adc);
}
