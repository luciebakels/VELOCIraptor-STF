/*! \file io.cxx
 *  \brief this file contains routines for io of baryonic analysis
 */

//-- IO

#include "baryoniccontent.h"

#include "../../src/gadgetitems.h"
#include "../../src/tipsy_structs.h"
#include "../../src/endianutils.h"


///Checks if file exits by attempting to get the file attributes
///If success file obviously exists.
///If failure may mean that we don't have permission to access the folder which contains this file or doesn't exist.
///If we need to do that level of checking, lookup return values of stat which will give you more details on why stat failed.
bool FileExists(const char *fname) {
  struct stat stFileInfo;
  bool blnReturn;
  int intStat;
  intStat = stat(fname,&stFileInfo);
  if(intStat == 0) return  true;
  else return false;
}

///\name Final outputs such as properties  
//@{
///Writes the bulk properties
void WriteProperties(Options &opt, const Int_t ngroups, PropData *pdata, int ptype, int ibound){
    fstream Fout;
    char fname[1000];
    char fname2[1000];
    Int_t noffset=0,ngtot=0;
#ifndef USEMPI
    int ThisTask=0,NProcs=1;
#endif
/*
#ifdef USEMPI
    for (int j=0;j<NProcs;j++) ngtot+=mpi_ngroups[j];
    for (int j=0;j<ThisTask;j++)noffset+=mpi_ngroups[j];
#else
    ngtot=ngroups;
#endif
*/
    ngtot=ngroups;
    if (ptype==GASTYPE) sprintf(fname,"%s.properties.gas",opt.outname);
    else if (ptype==DMTYPE) sprintf(fname,"%s.properties.dm",opt.outname);
    else if (ptype==STARTYPE) sprintf(fname,"%s.properties.star",opt.outname);
    if (ibound==1) {
        sprintf(fname2,"%s.bound",fname);
        sprintf(fname,"%s",fname2);
    }
    if (ibound==-1) {
        sprintf(fname2,"%s.unbound",fname);
        sprintf(fname,"%s",fname2);
    }
/*
#ifdef USEMPI
    sprintf(fname2,"%s.%d",fname,ThisTask);
    sprintf(fname,"%s",fname2);
#endif
*/

    //write header
    if (opt.ibinaryout) {
    Fout.open(fname,ios::out|ios::binary);
    Fout.write((char*)&ThisTask,sizeof(int));
    Fout.write((char*)&NProcs,sizeof(int));
    Fout.write((char*)&ngroups,sizeof(Int_t));
    Fout.write((char*)&ngtot,sizeof(Int_t));
    Fout.write((char*)&ptype,sizeof(int));
    }
    else {
    Fout.open(fname,ios::out);
    Fout<<ThisTask<<" "<<NProcs<<endl;
    Fout<<ngroups<<" "<<ngtot<<endl;
    Fout<<ptype<<endl;
    Fout<<setprecision(10);
    }

    long long idbound;
    //for ensuring downgrade of precision as subfind uses floats when storing values save for Mvir (??why??)
    float value,ctemp[3],mtemp[9];
    double dvalue;
    int ivalue;
    for (Int_t i=0;i<ngroups;i++) {
        if (opt.ibinaryout) {
        dvalue=pdata[i].gMvir;
        Fout.write((char*)&dvalue,sizeof(dvalue));
        ivalue=pdata[i].num;
        Fout.write((char*)&ivalue,sizeof(ivalue));
        for (int k=0;k<3;k++) ctemp[k]=pdata[i].gcm[k];
        Fout.write((char*)ctemp,sizeof(float)*3);
        for (int k=0;k<3;k++) ctemp[k]=pdata[i].gpos[k];
        Fout.write((char*)ctemp,sizeof(float)*3);
        for (int k=0;k<3;k++) ctemp[k]=pdata[i].gcmvel[k];
        Fout.write((char*)ctemp,sizeof(float)*3);
        for (int k=0;k<3;k++) ctemp[k]=pdata[i].gvel[k];
        Fout.write((char*)ctemp,sizeof(float)*3);
        value=pdata[i].gRvir;
        Fout.write((char*)&value,sizeof(value));
        value=pdata[i].gRmbp;
        Fout.write((char*)&value,sizeof(value));
        value=pdata[i].gRmaxvel;
        Fout.write((char*)&value,sizeof(value));
        value=pdata[i].gmaxvel;
        Fout.write((char*)&value,sizeof(value));
        value=pdata[i].gsigma_v;
        Fout.write((char*)&value,sizeof(value));
        for (int k=0;k<3;k++) ctemp[k]=pdata[i].gJ[k];
        Fout.write((char*)ctemp,sizeof(float)*3);
        value=pdata[i].gq;
        Fout.write((char*)&value,sizeof(value));
        value=pdata[i].gs;
        Fout.write((char*)&value,sizeof(value));
        for (int k=0;k<3;k++) for (int n=0;n<3;n++) mtemp[k*3+n]=pdata[i].geigvec(k,n);
        Fout.write((char*)mtemp,sizeof(float)*9);
        dvalue=pdata[i].gmass;
        Fout.write((char*)&dvalue,sizeof(dvalue));
        dvalue=pdata[i].gMmaxvel;
        Fout.write((char*)&dvalue,sizeof(dvalue));

        for (int k=0;k<3;k++) for (int n=0;n<3;n++) mtemp[k*3+n]=pdata[i].gveldisp(k,n);
        Fout.write((char*)mtemp,sizeof(float)*9);

        value=pdata[i].T;
        Fout.write((char*)&value,sizeof(value));
        value=pdata[i].Pot;
        Fout.write((char*)&value,sizeof(value));
        value=pdata[i].Efrac;
        Fout.write((char*)&value,sizeof(value));
        value=pdata[i].Ttyped[pdata[i].ptype];
        Fout.write((char*)&value,sizeof(value));
        value=pdata[i].Pottyped[pdata[i].ptype];
        Fout.write((char*)&value,sizeof(value));
        value=pdata[i].Efractyped[pdata[i].ptype];
        Fout.write((char*)&value,sizeof(value));

        //for (int k=0;k<1;k++) Fout.write((char*)&value,sizeof(value));//for fact that 2*2*4+3*4
        }
        else {
        Fout<<pdata[i].gMvir<<" ";
        Fout<<pdata[i].num<<" ";
        for (int k=0;k<3;k++) Fout<<pdata[i].gcm[k]<<" ";
        for (int k=0;k<3;k++) Fout<<pdata[i].gpos[k]<<" ";
        for (int k=0;k<3;k++) Fout<<pdata[i].gcmvel[k]<<" ";
        for (int k=0;k<3;k++) Fout<<pdata[i].gvel[k]<<" ";
        Fout<<pdata[i].gRvir<<" ";
        Fout<<pdata[i].gRmbp<<" ";
        Fout<<pdata[i].gRmaxvel<<" ";
        Fout<<pdata[i].gmaxvel<<" ";
        Fout<<pdata[i].gsigma_v<<" ";
        for (int k=0;k<3;k++) Fout<<pdata[i].gJ[k]<<" ";
        Fout<<pdata[i].gq<<" ";
        Fout<<pdata[i].gs<<" ";
        for (int k=0;k<3;k++) for (int n=0;n<3;n++) Fout<<pdata[i].geigvec(k,n)<<" ";
        Fout<<pdata[i].gmass<<" ";
        Fout<<pdata[i].gMmaxvel<<" ";
        for (int k=0;k<3;k++) for (int n=0;n<3;n++) Fout<<pdata[i].gveldisp(k,n)<<" ";
        Fout<<pdata[i].T<<" ";
        Fout<<pdata[i].Pot<<" ";
        Fout<<pdata[i].Efrac<<" ";
        Fout<<pdata[i].Ttyped[pdata[i].ptype]<<" ";
        Fout<<pdata[i].Pottyped[pdata[i].ptype]<<" ";
        Fout<<pdata[i].Efractyped[pdata[i].ptype]<<" ";
        }
        if (ptype==GASTYPE) {
        if (opt.ibinaryout) {
        for (int j=0;j<opt.nquants+2;j++) {
            value=pdata[i].Temp[j];
            Fout.write((char*)&value,sizeof(value));
        }
#ifdef RADIATIVE
        for (int j=0;j<opt.nquants+2;j++) {
            value=pdata[i].Zmet[j];
            Fout.write((char*)&value,sizeof(value));
        }
#endif
        }
        else {
        for (int j=0;j<opt.nquants+2;j++) Fout<<pdata[i].Temp[j]<<" ";
#ifdef RADIATIVE
        for (int j=0;j<opt.nquants+2;j++) Fout<<pdata[i].Zmet[j]<<" ";
#endif
        }
        }
#ifdef RADIATIVE
        else if (ptype==STARTYPE) {
        if (opt.ibinaryout) {
        for (int j=0;j<opt.nquants+2;j++) {
            value=pdata[i].Tage[j];
            Fout.write((char*)&value,sizeof(value));
        }
        for (int j=0;j<opt.nquants+2;j++) {
            value=pdata[i].Zmet[j];
            Fout.write((char*)&value,sizeof(value));
        }
        }
        else {
        for (int j=0;j<opt.nquants+2;j++) Fout<<pdata[i].Tage[j]<<" ";
        for (int j=0;j<opt.nquants+2;j++) Fout<<pdata[i].Zmet[j]<<" ";
        }
        }
#endif
        if (opt.ibinaryout==0) Fout<<endl;
    }
    cout<<"Done"<<endl;
    Fout.close();
}

///Write radial profiles 
void WriteProfiles(Options &opt, const Int_t ngroups, PropData *pdata, int ptype, int ibound){
    fstream Fout;
    char fname[1000];
    char fname2[1000];
    Int_t noffset=0,ngtot=0;
#ifndef USEMPI
    int ThisTask=0,NProcs=1;
#endif
/*
#ifdef USEMPI
    for (int j=0;j<NProcs;j++) ngtot+=mpi_ngroups[j];
    for (int j=0;j<ThisTask;j++)noffset+=mpi_ngroups[j];
#else
    ngtot=ngroups;
#endif
*/
    ngtot=ngroups;
    if (ptype==GASTYPE) sprintf(fname,"%s.profiles.gas",opt.outname);
    else if (ptype==DMTYPE) sprintf(fname,"%s.profiles.dm",opt.outname);
    else if (ptype==STARTYPE) sprintf(fname,"%s.profiles.star",opt.outname);
    if (ibound==1) {
      sprintf(fname2,"%s.bound",fname);
      sprintf(fname,"%s",fname2);
    }
    if (ibound==-1) {
      sprintf(fname2,"%s.unbound",fname);
      sprintf(fname,"%s",fname2);
    }
/*
#ifdef USEMPI
      sprintf(fname2,"%s.%d",fname,ThisTask);
      sprintf(fname,"%s",fname2);
#endif
*/

    if (opt.ibinaryout) {
    Fout.open(fname,ios::out|ios::binary);
    Fout.write((char*)&ThisTask,sizeof(int));
    Fout.write((char*)&NProcs,sizeof(int));
    Fout.write((char*)&ngroups,sizeof(Int_t));
    Fout.write((char*)&ngtot,sizeof(Int_t));
    Fout.write((char*)&ptype,sizeof(int));
    }
    else {
    Fout.open(fname,ios::out);
    Fout<<ThisTask<<" "<<NProcs<<endl;
    Fout<<ngroups<<" "<<ngtot<<endl;
    Fout<<ptype<<endl;
    Fout<<setprecision(10);
    }
    for (Int_t i=0;i<ngroups;i++) {
        if (opt.ibinaryout) {
        Fout.write((char*)&pdata[i].radprofile.nbins,sizeof(int));
        Fout.write((char*)pdata[i].radprofile.radval,sizeof(Double_t)*pdata[i].radprofile.nbins);
        Fout.write((char*)pdata[i].radprofile.Menc,sizeof(Double_t)*pdata[i].radprofile.nbins);
        Fout.write((char*)pdata[i].radprofile.velenc,sizeof(Coordinate)*pdata[i].radprofile.nbins);
        Fout.write((char*)pdata[i].radprofile.sigmavenc,sizeof(Matrix)*pdata[i].radprofile.nbins);
        Fout.write((char*)pdata[i].radprofile.Jenc,sizeof(Coordinate)*pdata[i].radprofile.nbins);
        Fout.write((char*)pdata[i].radprofile.qenc,sizeof(Double_t)*pdata[i].radprofile.nbins);
        Fout.write((char*)pdata[i].radprofile.senc,sizeof(Double_t)*pdata[i].radprofile.nbins);
        Fout.write((char*)pdata[i].radprofile.eigvecenc,sizeof(Matrix)*pdata[i].radprofile.nbins);
        }
        else {
        Fout<<pdata[i].radprofile.nbins<<" "<<pdata[i].num<<endl;
        for (Int_t j=0;j<pdata[i].radprofile.nbins;j++) {
            Fout<<j<<" "<<pdata[i].radprofile.numinbin[j]<<" "<<pdata[i].radprofile.radval[j]<<" "<<pdata[i].radprofile.Menc[j]<<" ";
            for (int k=0;k<3;k++) Fout<<pdata[i].radprofile.velenc[j][k]<<" ";
            for (int k=0;k<3;k++) for (int l=0;l<3;l++)Fout<<pdata[i].radprofile.sigmavenc[j](k,l)<<" ";
            for (int k=0;k<3;k++) Fout<<pdata[i].radprofile.Jenc[j][k]<<" ";
            Fout<<pdata[i].radprofile.qenc[j]<<" "<<pdata[i].radprofile.senc[j]<<" ";
            for (int k=0;k<3;k++) for (int l=0;l<3;l++) Fout<<pdata[i].radprofile.eigvecenc[j](k,l)<<" ";
            Fout<<endl;
        }
        Fout<<endl;
        }
    }
    //additional output depending on particle type
    if (ptype==GASTYPE) {
    for (Int_t i=0;i<ngroups;i++) {
        if (opt.ibinaryout) {
        Fout.write((char*)&pdata[i].radprofile.nbins,sizeof(int));
        Fout.write((char*)pdata[i].radprofile.Tempenc,sizeof(Double_t)*pdata[i].radprofile.nbins);
        Fout.write((char*)pdata[i].radprofile.Tdispenc,sizeof(Double_t)*pdata[i].radprofile.nbins);
        Fout.write((char*)pdata[i].radprofile.Temenc,sizeof(Double_t)*pdata[i].radprofile.nbins);
        Fout.write((char*)pdata[i].radprofile.Tslenc,sizeof(Double_t)*pdata[i].radprofile.nbins);
        Fout.write((char*)pdata[i].radprofile.Den,sizeof(Double_t)*pdata[i].radprofile.nbins);
#ifdef RADIATIVE
        Fout.write((char*)pdata[i].radprofile.Zmetenc,sizeof(Double_t)*pdata[i].radprofile.nbins);
#endif
        }
        else {
        Fout<<pdata[i].radprofile.nbins<<" "<<endl;
        for (Int_t j=0;j<pdata[i].radprofile.nbins;j++) {
            Fout<<j<<" "<<pdata[i].radprofile.radval[j]<<" "<<pdata[i].radprofile.Tempenc[j]<<" "<<pdata[i].radprofile.Den[j]<<" ";
#ifdef RADIATIVE
            Fout<<pdata[i].radprofile.Zmetenc[j]<<" ";
#endif
            Fout<<endl;
        }
        Fout<<endl;
        }
    }
    }
#ifdef RADIATIVE
    if (ptype==STARTYPE) {
    for (Int_t i=0;i<ngroups;i++) {
        if (opt.ibinaryout) {
        Fout.write((char*)&pdata[i].radprofile.nbins,sizeof(int));
        Fout.write((char*)pdata[i].radprofile.Tageenc,sizeof(Double_t)*pdata[i].radprofile.nbins);
        Fout.write((char*)pdata[i].radprofile.Zmetenc,sizeof(Double_t)*pdata[i].radprofile.nbins);
        }
        else {
        Fout<<pdata[i].radprofile.nbins<<" "<<endl;
        for (Int_t j=0;j<pdata[i].radprofile.nbins;j++) {
            Fout<<j<<" "<<pdata[i].radprofile.radval[j]<<" "<<pdata[i].radprofile.Tageenc[j]<<" "<<pdata[i].radprofile.Zmetenc[j];
            Fout<<endl;
        }
        Fout<<endl;
        }
    }
    }
#endif

    Fout.close();
}

///Write cylindrical profiles 
void WriteCylProfiles(Options &opt, const Int_t ngroups, PropData *pdata, int ptype, int ibound){
    fstream Fout;
    char fname[1000];
    char fname2[1000];
    Int_t noffset=0,ngtot=0;
#ifndef USEMPI
    int ThisTask=0,NProcs=1;
#endif
/*
#ifdef USEMPI
    for (int j=0;j<NProcs;j++) ngtot+=mpi_ngroups[j];
    for (int j=0;j<ThisTask;j++)noffset+=mpi_ngroups[j];
#else
    ngtot=ngroups;
#endif
*/
    ngtot=ngroups;
    if (ptype==GASTYPE) sprintf(fname,"%s.cylprofiles.gas",opt.outname);
    else if (ptype==DMTYPE) sprintf(fname,"%s.cylprofiles.dm",opt.outname);
    else if (ptype==STARTYPE) sprintf(fname,"%s.cylprofiles.star",opt.outname);
    if (ibound==1) {
      sprintf(fname2,"%s.bound",fname);
      sprintf(fname,"%s",fname2);
    }
    if (ibound==-1) {
      sprintf(fname2,"%s.unbound",fname);
      sprintf(fname,"%s",fname2);
    }
/*
#ifdef USEMPI
      sprintf(fname2,"%s.%d",fname,ThisTask);
      sprintf(fname,"%s",fname2);
#endif
*/

    if (opt.ibinaryout) {
    Fout.open(fname,ios::out|ios::binary);
    Fout.write((char*)&ThisTask,sizeof(int));
    Fout.write((char*)&NProcs,sizeof(int));
    Fout.write((char*)&ngtot,sizeof(Int_t));
    Fout.write((char*)&ngroups,sizeof(Int_t));
    Fout.write((char*)&ptype,sizeof(int));
    }
    else {
    Fout.open(fname,ios::out);
    Fout<<ThisTask<<" "<<NProcs<<endl;
    Fout<<ngtot<<" "<<ngroups<<endl;
    Fout<<ptype<<endl;
    Fout<<setprecision(10);
    }
    for (Int_t i=0;i<ngroups;i++) {
        if (opt.ibinaryout) {
        Fout.write((char*)&pdata[i].cylprofile.nbins,sizeof(int));
        Fout.write((char*)pdata[i].cylprofile.radval,sizeof(Double_t)*pdata[i].cylprofile.nbins);
        Fout.write((char*)pdata[i].cylprofile.Menc,sizeof(Double_t)*pdata[i].cylprofile.nbins);
        Fout.write((char*)pdata[i].cylprofile.velenc,sizeof(Coordinate)*pdata[i].cylprofile.nbins);
        Fout.write((char*)pdata[i].cylprofile.sigmavenc,sizeof(Matrix)*pdata[i].cylprofile.nbins);
        Fout.write((char*)pdata[i].cylprofile.Jenc,sizeof(Coordinate)*pdata[i].cylprofile.nbins);
        Fout.write((char*)pdata[i].cylprofile.zmean,sizeof(Double_t)*pdata[i].cylprofile.nbins);
        Fout.write((char*)pdata[i].cylprofile.zdisp,sizeof(Double_t)*pdata[i].cylprofile.nbins);
        Fout.write((char*)pdata[i].cylprofile.Rmean,sizeof(Double_t)*pdata[i].cylprofile.nbins);
        Fout.write((char*)pdata[i].cylprofile.Rdisp,sizeof(Double_t)*pdata[i].cylprofile.nbins);
        Fout.write((char*)pdata[i].cylprofile.qenc,sizeof(Double_t)*pdata[i].cylprofile.nbins);
        Fout.write((char*)pdata[i].cylprofile.senc,sizeof(Double_t)*pdata[i].cylprofile.nbins);
        Fout.write((char*)pdata[i].cylprofile.eigvecenc,sizeof(Matrix)*pdata[i].cylprofile.nbins);
        }
        else {
        Fout<<pdata[i].cylprofile.nbins<<" "<<endl;
        for (Int_t j=0;j<pdata[i].cylprofile.nbins;j++) {
            Fout<<j<<" "<<pdata[i].cylprofile.numinbin[j]<<" "<<pdata[i].cylprofile.radval[j]<<" "<<pdata[i].cylprofile.Menc[j]<<" ";
            for (int k=0;k<3;k++) Fout<<pdata[i].cylprofile.velenc[j][k]<<" ";
            for (int k=0;k<3;k++) for (int l=0;l<3;l++)Fout<<pdata[i].cylprofile.sigmavenc[j](k,l)<<" ";
            for (int k=0;k<3;k++) Fout<<pdata[i].cylprofile.Jenc[j][k]<<" ";
            Fout<<pdata[i].cylprofile.zmean[j]<<" "<<pdata[i].cylprofile.zdisp[j]<<" ";
            Fout<<pdata[i].cylprofile.Rmean[j]<<" "<<pdata[i].cylprofile.Rdisp[j]<<" ";
            Fout<<pdata[i].cylprofile.qenc[j]<<" "<<pdata[i].cylprofile.senc[j]<<" ";
            for (int k=0;k<3;k++) for (int l=0;l<3;l++) Fout<<pdata[i].cylprofile.eigvecenc[j](k,l)<<" ";
            Fout<<endl;
        }
        Fout<<endl;
        }
    }
    //additional output depending on particle type
    if (ptype==GASTYPE) {
    for (Int_t i=0;i<ngroups;i++) {
        if (opt.ibinaryout) {
        Fout.write((char*)&pdata[i].cylprofile.nbins,sizeof(int));
        Fout.write((char*)pdata[i].cylprofile.Tempenc,sizeof(Double_t)*pdata[i].cylprofile.nbins);
        Fout.write((char*)pdata[i].cylprofile.Tdispenc,sizeof(Double_t)*pdata[i].cylprofile.nbins);
        Fout.write((char*)pdata[i].cylprofile.Temenc,sizeof(Double_t)*pdata[i].cylprofile.nbins);
        Fout.write((char*)pdata[i].cylprofile.Tslenc,sizeof(Double_t)*pdata[i].cylprofile.nbins);
        Fout.write((char*)pdata[i].cylprofile.Den,sizeof(Double_t)*pdata[i].cylprofile.nbins);
#ifdef RADIATIVE
        Fout.write((char*)pdata[i].cylprofile.Zmetenc,sizeof(Double_t)*pdata[i].cylprofile.nbins);
#endif
        }
        else {
        Fout<<pdata[i].cylprofile.nbins<<" "<<endl;
        for (Int_t j=0;j<pdata[i].cylprofile.nbins;j++) {
            Fout<<j<<" "<<pdata[i].cylprofile.radval[j]<<" "<<pdata[i].cylprofile.Tempenc[j]<<" "<<pdata[i].cylprofile.Den[j]<<" ";
#ifdef RADIATIVE
            Fout<<pdata[i].cylprofile.Zmetenc[j]<<" ";
#endif
            Fout<<endl;
        }
        Fout<<endl;
        }
    }
    }
#ifdef RADIATIVE
    if (ptype==STARTYPE) {
    for (Int_t i=0;i<ngroups;i++) {
        if (opt.ibinaryout) {
        Fout.write((char*)&pdata[i].cylprofile.nbins,sizeof(int));
        Fout.write((char*)pdata[i].cylprofile.Tageenc,sizeof(Double_t)*pdata[i].cylprofile.nbins);
        Fout.write((char*)pdata[i].cylprofile.Zmetenc,sizeof(Double_t)*pdata[i].cylprofile.nbins);
        }
        else {
        Fout<<pdata[i].cylprofile.nbins<<" "<<endl;
        for (Int_t j=0;j<pdata[i].cylprofile.nbins;j++) {
            Fout<<j<<" "<<pdata[i].cylprofile.radval[j]<<" "<<pdata[i].cylprofile.Tageenc[j]<<" "<<pdata[i].cylprofile.Zmetenc[j];
            Fout<<endl;
        }
        Fout<<endl;
        }
    }
    }
#endif
    Fout.close();

}
//@}
