/*! \file main.cxx
 *  \brief Main program

*/

#include "baryoniccontent.h"

using namespace std;
using namespace Math;
using namespace NBody;

int main(int argc,char **argv)
{
#ifdef USEMPI
    //start MPI
    MPI_Init(&argc,&argv);
    //find out how big the SPMD world is
    MPI_Comm_size(MPI_COMM_WORLD,&NProcs);
    //and this processes' rank is
    MPI_Comm_rank(MPI_COMM_WORLD,&ThisTask);

#else
    int ThisTask=0,NProcs=1,Nlocal,Ntotal;
#endif
    ///\todo need to implement mpi routines. At the moment, just ThisTask zero does anything
    //if (ThisTask==0) {

    HaloParticleData *halodata;
    ///store properties for all particles whether bound or not
    PropData *allprops[NPARTTYPES],*haloprop,*gasprop,*starprop;
    ///for only bound particles
    PropData *ballprops[NPARTTYPES],*bhaloprop,*bgasprop,*bstarprop;
    ///for only unbound particles
    PropData *ubhaloprop,*ubgasprop,*ubstarprop;
    Particle *Part;
    Int_t nhalos,nbodies;
    Options opt;

    GetArgs(argc, argv, opt);

    ///\todo to make this mpi, one must either use the implied mpi decomposition in the
    ///VELOCIraptor files or define a new decomposition. Will have to also set nbodies=Nlocal;
    //halo information information and allocate memory
    cout<<"Read Data ... "<<endl;
    halodata=ReadHaloGroupCatalogData(opt.halocatname,nhalos,nbodies,opt.mpinum,opt.ibinaryin,opt.iseparatefiles);
    if (nhalos>0) {
    //haloprop=ReadHaloPropertiesData(opt.halocatname,nhalos,opt.mpinum,opt.ibinaryin,opt.iseparatefiles);
    haloprop=ReadHaloPropertiesData(opt.halocatname,nhalos,opt.mpinum,1,opt.iseparatefiles);
    bhaloprop=new PropData[nhalos];
    for (int i=0;i<nhalos;i++) {haloprop[i].ptype=DMTYPE;bhaloprop[i].ptype=DMTYPE;}
    cout<<nhalos<<" objects with properties of size "<<sizeof(PropData)<<" requiring "<<nhalos*2*3*sizeof(PropData)/1024./1024.0/1024.0<<" GB of memory"<<endl;
    //allocate memory
    cout<<nbodies<<" particles of size "<<sizeof(Particle)<<" requiring "<<nbodies*sizeof(Particle)/1024./1024.0/1024.0<<" GB of memory"<<endl;
    Part=new Particle[nbodies];//all the particles in structures
    //if zoom simulation, there's no simple id to index mapping so
    //specific the effective resolution and then build id to index map
    ReadParticleData(opt,nbodies,Part,nhalos,halodata);
    //for gas bulk properties
    if (opt.npart[GASTYPE]>0) {
        gasprop=new PropData[nhalos];
        for (int i=0;i<nhalos;i++) gasprop[i].ptype=GASTYPE;
        bgasprop=new PropData[nhalos];
        for (int i=0;i<nhalos;i++) bgasprop[i].ptype=GASTYPE;
    }
    //for star bulk properties
    if (opt.npart[STARTYPE]>0) {
        starprop=new PropData[nhalos];
        for (int i=0;i<nhalos;i++) starprop[i].ptype=STARTYPE;
        bstarprop=new PropData[nhalos];
        for (int i=0;i<nhalos;i++) bstarprop[i].ptype=STARTYPE;
    }
    cout<<opt.npart[GASTYPE]<<" gas particles"<<endl;
    cout<<opt.npart[DMTYPE]<<" dm particles"<<endl;
    cout<<opt.npart[STARTYPE]<<" star particles"<<endl;
    cout<<"Done Loading"<<endl;

    allprops[DMTYPE]=haloprop;
    allprops[GASTYPE]=gasprop;
    allprops[STARTYPE]=starprop;
    ballprops[DMTYPE]=bhaloprop;
    ballprops[GASTYPE]=bgasprop;
    ballprops[STARTYPE]=bstarprop;

    //adjust for periodicity
    AdjustForPeriod(opt,Part,nhalos,haloprop,halodata);

    //calculate CM of DM
    //GetCM(opt, Part, nhalos, allprops[DMTYPE], halodata, DMTYPE);
    //move all the particles into the dm cm reference frame
    //or move particles to most dm bound particle reference frame
    //or the deepest potential well reference frame
    //note that I have discontinued the IMBFRAME since this assumes that the particle
    //identified as MB by previous analysis is correct. 
    //note that I think its much better to use deepest potential well to define binding for the moment.
    //this also sets gpos correctly.
    if (opt.reframe==ICMFRAME) MovetoCMFrame(opt, Part, nhalos, haloprop, halodata);
    else MovetoPotFrame(opt, Part, nhalos, haloprop, halodata);
    //else if (opt.reframe==IMBFRAME) MovetoMBFrame(opt, Part, nhalos, haloprop, halodata);

    //first sort particle by binding energy (where for gas particles, self energy is included
    //in determining binding energy relative to dark matter halo
    SortAccordingtoBindingEnergy(opt, Part, nhalos, allprops, halodata,1,1,1);

    //and determine dm most bound particle for reference frame if move to pot 
    if (opt.reframe!=IPOTFRAME) GetMostBoundParticle(opt, Part, nhalos, allprops[DMTYPE], halodata);
    GetBulkProp(opt, Part, nhalos, allprops[DMTYPE], halodata, DMTYPE);
    for (int i=0;i<NPARTTYPES;i++) if (opt.npart[i]>0&&i!=DMTYPE) {
        GetCM(opt, Part, nhalos, allprops[i], halodata, i);
        GetMostBoundParticle(opt, Part, nhalos, allprops[i], halodata);
        GetBulkProp(opt, Part, nhalos, allprops[i], halodata, i);
    }
    for (int i=0;i<NPARTTYPES;i++) if (opt.npart[i]>0){
        GetProfiles(opt,Part,nhalos,allprops[i],halodata,i);
    }
    for (int i=0;i<NPARTTYPES;i++) if (opt.npart[i]>0) {
        WriteProperties(opt,nhalos,allprops[i],i);
        WriteProfiles(opt,nhalos,allprops[i],i);
    }

    //now sort particles according to binding energy
    //keep only bound particles
    GetBoundParticles(opt, Part, nhalos, allprops, halodata, opt.TVratio);

    allprops[DMTYPE]=bhaloprop;
    allprops[GASTYPE]=bgasprop;
    allprops[STARTYPE]=bstarprop;

    //and then calculate properties and profiles again
    for (int i=0;i<NPARTTYPES;i++) if (opt.npart[i]>0) {
        for (Int_t j=0;j<nhalos;j++) allprops[i][j].num=halodata[j].NumofType[i];
        GetCM(opt, Part, nhalos, allprops[i], halodata, i);
        GetMostBoundParticle(opt, Part, nhalos, allprops[i], halodata);
        GetBulkProp(opt, Part, nhalos, allprops[i], halodata, i);
        GetProfiles(opt,Part,nhalos,allprops[i],halodata,i);
        WriteProperties(opt,nhalos,allprops[i],i,1);
        WriteProfiles(opt,nhalos,allprops[i],i,1);
    }

    //now cylindrical profiles
    allprops[DMTYPE]=haloprop;
    allprops[GASTYPE]=gasprop;
    allprops[STARTYPE]=starprop;
    for (Int_t i=0;i<nhalos;i++) {
        halodata[i].NumberofParticles=halodata[i].AllNumberofParticles;
        for (int j=0;j<NPARTTYPES;j++) if (opt.npart[j]>0) halodata[i].NumofType[j]=halodata[i].AllNumofType[j];
    }

    GetCylFrame(opt,Part,nhalos,allprops[DMTYPE],halodata);
    for (int i=0;i<NPARTTYPES;i++) if (opt.npart[i]>0) {
        GetCylProfiles(opt,Part,nhalos,allprops[i],halodata,i);
        WriteCylProfiles(opt,nhalos,allprops[i],i);
    }

    //now cylindrical profiles
    allprops[DMTYPE]=bhaloprop;
    allprops[GASTYPE]=bgasprop;
    allprops[STARTYPE]=bstarprop;
    for (Int_t i=0;i<nhalos;i++) {
        halodata[i].NumberofParticles=0;
        for (int j=0;j<NPARTTYPES;j++)if (opt.npart[j]>0) {
            halodata[i].NumofType[j]=allprops[j][i].num;
            halodata[i].NumberofParticles+=allprops[j][i].num;
        }
    }

    for (int i=0;i<NPARTTYPES;i++) if (opt.npart[i]>0) {
        GetCylProfiles(opt,Part,nhalos,allprops[i],halodata,i);
        WriteCylProfiles(opt,nhalos,allprops[i],i,1);
    }

    }
    else {
        cout<<"No halos "<<endl;
        allprops[DMTYPE]=haloprop;
        allprops[GASTYPE]=gasprop;
        allprops[STARTYPE]=starprop;
        ballprops[DMTYPE]=bhaloprop;
        ballprops[GASTYPE]=bgasprop;
        ballprops[STARTYPE]=bstarprop;
        for (int i=0;i<NPARTTYPES;i++) if (opt.npart[i]>0) {
            WriteProperties(opt,nhalos,allprops[i],i);
            WriteProperties(opt,nhalos,allprops[i],i,1);
            WriteProfiles(opt,nhalos,allprops[i],i);
            WriteProfiles(opt,nhalos,allprops[i],i,1);
            WriteCylProfiles(opt,nhalos,allprops[i],i);
            WriteCylProfiles(opt,nhalos,allprops[i],i,1);
        }
        cout<<"Done writing files containing just a header "<<endl;
    }
#ifdef USEMPI
    MPI_Finalize();
#endif
    return 0;
}
