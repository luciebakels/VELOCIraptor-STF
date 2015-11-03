/*! \file search.cxx
 *  \brief this file contains routines that search particle list using FOF routines
 */

//--  Suboutines that search particle list

#include "stf.h"

/// \name Searches full system 
//@{

/*! 
    Search full system without finding outliers first
    Here search simulation for FOF haloes (either 3d or 6d). Note for 6D search, first 3d FOF halos are found, then velocity scale is set by largest 3DFOF
    halo, and 6DFOF search is done.
*/
Int_t* SearchFullSet(Options &opt, const Int_t nbodies, Particle *&Part, Int_t &numgroups)
{
    Int_t i, *pfof,*pfoftemp, minsize;
    FOFcompfunc fofcmp;
    fstream Fout;
    char fname[2000];
    Double_t param[20];
    Double_t *period=NULL;
    Double_t vscale2,mtotregion,vx,vy,vz;
    Coordinate vmean(0,0,0);
    int maxnthreads,nthreads=1,tid;
    Int_t *Len,*Head,*Next,*Tail, *storetype, *ids,*numingroup=NULL;
    Int_t ng,npartingroups;
    Int_t totalgroups;
#ifndef USEMPI
    int ThisTask=0,NProcs=1;
    Int_t Nlocal=nbodies;
#endif
#ifdef USEOPENMP
#pragma omp parallel
    {
    if (omp_get_thread_num()==0) maxnthreads=omp_get_num_threads();
    if (omp_get_thread_num()==0) nthreads=omp_get_num_threads();
    }
#endif

if (opt.p>0) {
        period=new Double_t[3];
        for (int j=0;j<3;j++) period[j]=opt.p;
    }
    psldata=new StrucLevelData;

    minsize=opt.HaloMinSize;
#ifdef USEMPI
    //if using MPI, lower minimum number
    minsize=MinNumMPI;
#endif

    cout<<"Begin FOF search  of entire particle data set ... "<<endl;
    KDTree *tree;
    param[0]=tree->TPHYS;
    param[1]=(opt.ellxscale*opt.ellxscale)*(opt.ellphys*opt.ellphys)*(opt.ellhalophysfac*opt.ellhalophysfac);
    param[6]=param[1];
    cout<<"First build tree ... "<<endl;
    tree=new KDTree(Part,nbodies,opt.Bsize,tree->TPHYS,tree->KEPAN,1000,0,0,0,period);
    cout<<"Done"<<endl;
    cout<<"Search particles using 3DFOF in physical space"<<endl;
    cout<<"Parameters used are : ellphys="<<sqrt(param[6])<<" Lunits (and likely "<<sqrt(param[6])/opt.ellxscale<<" in interparticle spacing"<<endl;
    fofcmp=&FOF3d;
    //if using mpi no need to locally sort just yet and might as well return the Head, Len, Next arrays
#ifdef USEMPI
    Head=new Int_t[nbodies];Next=new Int_t[nbodies];
    pfof=tree->FOF(sqrt(param[1]),numgroups,minsize,0,Head,Next);
#else
    pfof=tree->FOF(sqrt(param[1]),numgroups,minsize,1);
#endif

#ifndef USEMPI
    totalgroups=numgroups;
    //if this flag is set, calculate localfield value here for particles possibly resident in a field structure
#ifdef STRUCDEN
    if (numgroups>0) {
    storetype=new Int_t[nbodies];
    for (i=0;i<nbodies;i++) storetype[i]=Part[i].GetType();
    for (i=0;i<nbodies;i++) Part[i].SetType(pfof[Part[i].GetID()]);
    GetVelocityDensity(opt, nbodies, Part,tree);
    for (i=0;i<nbodies;i++) Part[i].SetType(storetype[i]);
    delete[] storetype;
    }
#endif
    delete tree;
#endif

#ifdef USEMPI
    mpi_foftask=new Int_t[Nlocal];
    MPISetTaskID(Nlocal);

    Len=new Int_t[nbodies];
    if (numgroups) {
        numingroup=BuildNumInGroup(nbodies, numgroups, pfof);
        for (i=0;i<nbodies;i++) Len[i]=numingroup[pfof[Part[i].GetID()]];
        delete[] numingroup;
        numingroup=NULL;
    }
    cout<<ThisTask<<" Finished local search "<<NExport<<" "<<NImport<<endl;

    //if using MPI must determine which local particles need to be exported to other threads and used to search
    //that threads particles. This is done by seeing if the any particles have a search radius that overlaps with 
    //the boundaries of another threads domain. Then once have exported particles must search local particles
    //relative to these exported particles. Iterate over search till no new links are found.

    //Also must ensure that group ids do not overlap between mpi threads so adjust group ids
    MPI_Allgather(&numgroups, 1, MPI_Int_t, mpi_ngroups, 1, MPI_Int_t, MPI_COMM_WORLD);
    MPIAdjustLocalGroupIDs(nbodies, pfof);

    //then determine export particles, declare arrays used to export data
#ifdef MPIREDUCEMEM
    MPIGetExportNum(nbodies, Part, sqrt(param[1]));
#endif

    PartDataIn = new Particle[NExport];
    PartDataGet = new Particle[NImport];
    FoFDataIn = new fofdata_in[NExport];
    FoFDataGet = new fofdata_in[NImport];
    //I have adjusted FOF data structure to have local group length and also seperated the export particles from export fof data
    //the reason is that will have to update fof data in iterative section but don't need to update particle information.
    MPIBuildParticleExportList(nbodies, Part, pfof, Len, sqrt(param[1]));
    MPI_Barrier(MPI_COMM_WORLD);
    //Now that have FoFDataGet (the exported particles) must search local volume using said particles
    //This is done by finding all particles in the search volume and then checking if those particles meet the FoF criterion
    //One must keep iterating till there are no new links. 
    //Wonder if i don't need another loop and a final check
    Int_t links_across,links_across_total;
    cout<<ThisTask<<" Starting to linking across MPI domains"<<endl;
    do {
        links_across=MPILinkAcross(nbodies, tree, Part, pfof, Len, Head, Next, param[1]);
        MPI_Allreduce(&links_across, &links_across_total, 1, MPI_Int_t, MPI_SUM, MPI_COMM_WORLD);
        MPIUpdateExportList(nbodies,Part,pfof,Len);
    }while(links_across_total>0);
    cout<<ThisTask<<" done"<<endl;

    delete[] FoFDataIn;
    delete[] FoFDataGet;
    delete[] PartDataIn;
    delete[] PartDataGet;

    //reorder local particle array and delete memory associated with Head arrays, only need to keep Particles, pfof and some id and idexing information
    delete tree;
    delete[] Head;
    delete[] Next;
    delete[] Len;
    //Now redistribute groups so that they are local to a processor (also orders the group ids according to size
    opt.HaloMinSize=MinNumOld;//reset minimum size
    Int_t newnbodies=MPIGroupExchange(nbodies,Part,pfof);
    //once groups are local, can free up memory
#ifdef MPIREDUCEMEM
    if (Nmemlocal<Nlocal) {
#endif
    delete[] Part;
    //store new particle data in mpi_Part1 as external variable ensures memory allocated is not deallocated when function returns
    mpi_Part1=new Particle[newnbodies];
    Part=mpi_Part1;
    //delete[] mpi_idlist;//since particles have now moved, must generate new list
    //mpi_idlist=new Int_t[newnbodies];
#ifdef MPIREDUCEMEM
    }
#endif
    delete[] mpi_foftask;
    delete[] pfof;
    pfof=new Int_t[newnbodies];
    //And compile the information and remove groups smaller than minsize
    numgroups=MPICompileGroups(newnbodies,Part,pfof,opt.HaloMinSize);
    cout<<"MPI thread "<<ThisTask<<" has found "<<numgroups<<endl;
    //free up memory now that only need to store pfof and global ids
    totalgroups=0;
    for (int j=0;j<NProcs;j++) totalgroups+=mpi_ngroups[j];
    if (ThisTask==0) cout<<"Total number of groups found is "<<totalgroups<<endl;
    Nlocal=newnbodies;
#endif
    cout<<"Done"<<endl;

    //if calculating velocity density only of particles resident in field structures large enough for substructure search
#if defined(STRUCDEN) && defined(USEMPI)
if (totalgroups>0)
    {
        storetype=new Int_t[Nlocal];
        for (i=0;i<Nlocal;i++) storetype[i]=Part[i].GetType();
        for (i=0;i<Nlocal;i++) Part[i].SetType((pfof[i]>0));
        tree=new KDTree(Part,Nlocal,opt.Bsize,tree->TPHYS,tree->KEPAN,100,0,0,0,period);
        GetVelocityDensity(opt, Nlocal, Part,tree);
        delete tree;
        for (i=0;i<Nlocal;i++) Part[i].SetType(storetype[i]);
        delete[] storetype;
    }
#endif

    //have now 3dfof groups local to a MPI thread and particles are back in index order that will be used from now on
    //note that from on, use Nlocal, which is altered in mpi but set to nbodies in non-mpi

    if (opt.fofbgtype==FOF6D && totalgroups>0) {
    //now if 6dfof search to overcome issues with 3DFOF by searching only particles that have been previously linked by the 3dfof
    //use the largest "halo" to determine an appropriate velocity scale
    cout<<"Sorting particles for 6dfof search "<<endl;
    npartingroups=0;
    //sort particles so that largest group is first, then all other groups. Allows quick calculation of velocity scale of largest halo
    Int_t iend=0;

#ifdef USEOPENMP
    //Int_t *storepid=new Int_t[Nlocal];
    storetype=new Int_t[Nlocal];
    if (numingroup==NULL) numingroup=new Int_t[numgroups+1];
    Int_t *noffset=new Int_t[numgroups+1];
    for (i=0;i<=numgroups;i++) numingroup[i]=noffset[i]=0;
    for (i=0;i<Nlocal;i++) {
        storetype[i]=Part[i].GetPID();
        Part[i].SetPID((pfof[i]==0)*Nlocal+(pfof[i]>0)*pfof[i]);
        npartingroups+=(Int_t)(pfof[i]>0);
        iend+=(pfof[i]==1);
        numingroup[pfof[i]]++;
    }
    for (i=2;i<=numgroups;i++) noffset[i]=noffset[i-1]+numingroup[i-1];
    gsl_heapsort(Part, Nlocal, sizeof(Particle), PIDCompare);
    for (i=0;i<Nlocal;i++) Part[i].SetPID(storetype[Part[i].GetID()]);
    delete[] storetype;

#else
    storetype=new Int_t[Nlocal];
    for (i=0;i<Nlocal;i++) {
        storetype[i]=Part[i].GetPID();
        Part[i].SetPID(2*(pfof[i]==0)+(pfof[i]>1));
        npartingroups+=(Int_t)(pfof[i]>0);
        iend+=(pfof[i]==1);
    }
    gsl_heapsort(Part, Nlocal, sizeof(Particle), PIDCompare);
    for (i=0;i<Nlocal;i++) Part[i].SetPID(storetype[Part[i].GetID()]);
    delete[] storetype;
#endif

    ids=new Int_t[Nlocal];
    for (i=0;i<Nlocal;i++) ids[i]=Part[i].GetID();
    //calculate velocity scale
    vscale2=mtotregion=vx=vy=vz=0;
    for (i=0;i<iend;i++) {
        vx+=Part[i].GetVelocity(0)*Part[i].GetMass();
        vy+=Part[i].GetVelocity(1)*Part[i].GetMass();
        vz+=Part[i].GetVelocity(2)*Part[i].GetMass();
        mtotregion+=Part[i].GetMass();
    }
    vmean[0]=vx/mtotregion;vmean[1]=vy/mtotregion;vmean[2]=vz/mtotregion;
    for (i=0;i<iend;i++) {
        for (int j=0;j<3;j++) vscale2+=pow(Part[i].GetVelocity(j)-vmean[j],2.0)*Part[i].GetMass();
    }
    if (mtotregion>0) vscale2/=mtotregion;

#ifdef USEMPI
    Double_t mpi_vscale2;
    MPI_Allreduce(&vscale2,&mpi_vscale2,1,MPI_Real_t,MPI_MAX,MPI_COMM_WORLD);
    vscale2=mpi_vscale2;
#endif
    //to account for fact that dispersion may not be high enough to link all outlying regions
    vscale2*=1.25*1.25;
    //set the velocity scale
    param[2]=(vscale2);
    param[7]=param[2];
    minsize=opt.HaloMinSize;
    cout<<"Search "<<npartingroups<<" particles using 6DFOF"<<endl;
    cout<<"Parameters used are : ellphys="<<sqrt(param[6])<<" Lunits, ellvel="<<sqrt(param[7])<<" Vunits."<<endl;
    fofcmp=&FOF6d;
#ifdef USEOPENMP
    Head=new Int_t[Nlocal];Next=new Int_t[Nlocal];Tail=new Int_t[Nlocal];Len=new Int_t[Nlocal];
    KDTree *treeomp[nthreads];
    Int_t **pfofomp;
    Int_t *ngomp;
    //determine chunks to search
    iend=numgroups;
    for (i=1;i<=numgroups;i++) if (numingroup[i]<=ompunbindnum) {iend=i;break;}
    numingroup[0]=0;
    for (i=iend;i<=numgroups;i++) numingroup[0]+=numingroup[i];
    numingroup[iend]=numingroup[0];

    pfofomp=new Int_t*[iend+1];
    ngomp=new Int_t[iend+1];
#pragma omp parallel default(shared) \
private(i,tid)
{
#pragma omp for schedule(dynamic,1) nowait
    for (i=1;i<=iend;i++) {
        tid=omp_get_thread_num();
        treeomp[tid]=new KDTree(&Part[noffset[i]],numingroup[i],opt.Bsize,treeomp[tid]->TPHYS,tree->KEPAN,100,0,0,0,period);
        pfofomp[i]=treeomp[tid]->FOFCriterion(fofcmp,param,ngomp[i],minsize,1,0,Pnocheck,&Head[noffset[i]],&Next[noffset[i]],&Tail[noffset[i]],&Len[noffset[i]]);
        delete treeomp[tid];
    }
}
    ng=0;
    for (i=0;i<Nlocal;i++) pfof[i]=0;
    for (i=1;i<=iend;i++) {
        for (int j=0;j<numingroup[i];j++) {
            pfof[ids[Part[noffset[i]+j].GetID()+noffset[i]]]=pfofomp[i][j]+(pfofomp[i][j]>0)*ng; 
        }
        ng+=ngomp[i];
        delete[] pfofomp[i];
    }
    delete[] ngomp;
    delete[] pfofomp;
    delete[] noffset;
    delete[] numingroup;
    delete[] Head;
    delete[] Tail;
    delete[] Next;
    delete[] Len;
    if (ng>0) {
        if (opt.iverbose) cout<<" reordering "<<ng<<" groups "<<endl;
        numingroup=BuildNumInGroup(Nlocal, ng, pfof);
        Int_t **pglist=BuildPGList(Nlocal, ng, numingroup, pfof);
        ReorderGroupIDs(ng, ng, numingroup, pfof, pglist);
        for (i=1;i<=ng;i++) delete[] pglist[i];
        delete[] pglist;
        delete[] numingroup;
    }
#else
    tree=new KDTree(Part,npartingroups,opt.Bsize,tree->TPHYS,tree->KEPAN,1000,0,0,0,period);
    pfoftemp=tree->FOFCriterion(fofcmp,param,ng,minsize,1);
    for (i=0;i<npartingroups;i++) pfof[ids[Part[i].GetID()]]=pfoftemp[Part[i].GetID()];
    delete[] pfoftemp;
    delete tree;
#endif

    for (i=0;i<npartingroups;i++) Part[i].SetID(ids[i]);
    gsl_heapsort(Part, Nlocal, sizeof(Particle), IDCompare);
    delete[] ids;
    numgroups=ng;
    }
#ifdef USEMPI
    if (opt.fofbgtype==FOF6D) {
    cout<<"MPI thread "<<ThisTask<<" has found "<<numgroups<<endl;
    MPI_Allgather(&numgroups, 1, MPI_Int_t, mpi_ngroups, 1, MPI_Int_t, MPI_COMM_WORLD);
    //free up memory now that only need to store pfof and global ids
    if (ThisTask==0) {
        int totalgroups=0;
        for (int j=0;j<NProcs;j++) totalgroups+=mpi_ngroups[j];
        cout<<"Total number of groups found is "<<totalgroups<<endl;
    }
    }
#endif

    //now that field structures have been identified, allocate enough memory for the psldata pointer,
    //allocate memory for lowest level in the substructure hierarchy, corresponding to field objects
    psldata->Allocate(numgroups);
    psldata->Initialize();
    if (opt.iverbose) cout<<ThisTask<<" Now store hierarchy information "<<endl;
    for (i=0;i<Nlocal;i++)
        if (pfof[i]>0) {
        if (psldata->gidhead[pfof[i]]==NULL) {
            //set the group id head pointer to the address within the pfof array
            psldata->gidhead[pfof[i]]=&pfof[i];
            //set particle pointer to particle address
            psldata->Phead[pfof[i]]=&Part[i];
            //set the parent pointers to appropriate addresss such that the parent and uber parent are the same as the groups head
            psldata->gidparenthead[pfof[i]]=&pfof[i];
            psldata->giduberparenthead[pfof[i]]=&pfof[i];
            //set structure type
            psldata->stypeinlevel[pfof[i]]=HALOSTYPE;
        }
        }
    psldata->stype=HALOSTYPE;
    if (opt.iverbose) cout<<ThisTask<<" Done storing halo substructre level data"<<endl;
    //if search was periodic, alter particle positions in structures so substructure search no longer has to be
    //periodic
    if (opt.p>0&&numgroups>0) {
        if (numgroups>0)AdjustStructureForPeriod(opt,Nlocal,Part,numgroups,pfof);
        delete[] period;
    }
    return pfof;
}

void AdjustStructureForPeriod(Options &opt, const Int_t nbodies, Particle *Part, Int_t numgroups, Int_t *pfof)
{
    Int_t i,j;
    Int_t *numingroup, **pglist;
    Coordinate c;
    double diff;
#ifndef USEMPI
    int ThisTask=0,NProcs=1;
#endif
    numingroup=BuildNumInGroup(nbodies, numgroups, pfof);
    pglist=BuildPGList(nbodies, numgroups, numingroup, pfof,Part);
    if (opt.iverbose) cout<<ThisTask<<" Adjusting for period "<<opt.p<<endl;
    for (i=1;i<=numgroups;i++) if (numingroup[i]>ompperiodnum) {
        c=Coordinate(Part[pglist[i][0]].GetPosition());
        Int_t j;
#ifdef USEOPENMP
#pragma omp parallel default(shared) \
private(j,diff)
{
#pragma omp for 
#endif
        for (j=1;j<numingroup[i];j++) {
            for (int k=0;k<3;k++) {
                diff=c[k]-Part[pglist[i][j]].GetPosition(k);
                if (diff<-0.5*opt.p) Part[pglist[i][j]].SetPosition(k,Part[pglist[i][j]].GetPosition(k)-opt.p);
                else if (diff>0.5*opt.p) Part[pglist[i][j]].SetPosition(k,Part[pglist[i][j]].GetPosition(k)+opt.p);
            }
        }
#ifdef USEOPENMP
}
#endif
    }
#ifdef USEOPENMP

#pragma omp parallel default(shared) \
private(i,c,diff)
{
#pragma omp for schedule(dynamic)
#endif
    for (i=1;i<=numgroups;i++) if (numingroup[i]<=ompperiodnum) {
        c=Coordinate(Part[pglist[i][0]].GetPosition());
        for (Int_t j=1;j<numingroup[i];j++) {
            for (int k=0;k<3;k++) {
                diff=c[k]-Part[pglist[i][j]].GetPosition(k);
                if (diff<-0.5*opt.p) Part[pglist[i][j]].SetPosition(k,Part[pglist[i][j]].GetPosition(k)-opt.p);
                else if (diff>0.5*opt.p) Part[pglist[i][j]].SetPosition(k,Part[pglist[i][j]].GetPosition(k)+opt.p);
            }
        }
    }
#ifdef USEOPENMP
}
#endif
}

//@}

///\name Search using outliers from background velocity distribution.
//@{

///Search subset
/*! 
    \todo there are issues with locality for iterative search, significance check, regarding MPI threads. Currently these processes assume all necessary particles
    are locally accessible to the threads domain. For iSingleHalo==0, that is a full search of all fof halos has been done, that is the case as halos are localized
    to a MPI domain before the subsearch is made. However, in the case a of a single halo which has been broken up into several mpi domains, I must think carefully
    how the search should be localized. It should definitely be localized prior to CheckSignificance and the search window across mpi domains should use the larger
    physical search window used by the iterative search if that has been called.
 */
Int_t* SearchSubset(Options &opt, const Int_t nbodies, const Int_t nsubset, Particle *&Partsubset, Int_t &numgroups, Int_t sublevel, Int_t *pnumcores)
{
    KDTree *tree;
    Int_t *pfof, i,ii;
    FOFcompfunc fofcmp;
    fstream Fout;
    char fname[200];
    Double_t param[20];
    int nsearch=opt.Nvel;
    Int_t **nnID;
    Double_t **dist2;
    int nthreads=1,maxnthreads,tid;
    Int_t *numingroup, **pglist, *GroupTail, *Head, *Next;
    Int_t bgoffset,*pfofbg,numgroupsbg;
    //initialize 
    if (pnumcores!=NULL) *pnumcores=0;
#ifndef USEMPI
    int ThisTask=0,NProcs=1;
#endif

#ifdef USEMPI
    //if using MPI on a single halo, lower minimum number
    if (opt.iSingleHalo) opt.MinSize=MinNumMPI;
#endif

    int minsize=opt.MinSize;

    //for parallel environment store maximum number of threads
#ifdef USEOPENMP
#pragma omp parallel
    {
    if (omp_get_thread_num()==0) maxnthreads=omp_get_num_threads();
    if (omp_get_thread_num()==0) nthreads=omp_get_num_threads();
    }
#endif
    //set parameters
    param[1]=(opt.ellxscale*opt.ellxscale)*(opt.ellphys*opt.ellphys);
    param[2]=(opt.ellvscale*opt.ellvscale)*(opt.ellvel*opt.ellvel);
    param[6]=(opt.ellxscale*opt.ellxscale)*(opt.ellphys*opt.ellphys);
    param[7]=(opt.Vratio);
    if (opt.foftype==FOF6DSUBSET) param[7]=(opt.ellvscale*opt.ellvscale)*(opt.ellvel*opt.ellvel);
    param[8]=cos(opt.thetaopen*M_PI);
    param[9]=opt.ellthreshold;
    //if iterating slightly increase constraints and decrease minimum number
    if (opt.iiterflag) {
        if (opt.iverbose) cout<<"Increasing thresholds to search for initial list.\n";
        param[1]*=opt.ellxfac*opt.ellxfac/4.0;
        param[6]*=opt.ellxfac*opt.ellxfac/4.0;
        if (opt.foftype==FOF6DSUBSET) param[7]*=opt.vfac*opt.vfac;
        else param[7]*=opt.vfac;
        param[8]=cos(opt.thetaopen*M_PI*opt.thetafac);
        param[9]=opt.ellthreshold*opt.ellfac;
        minsize*=opt.nminfac;
    }

    //Set fof type
    //@{
    if (opt.foftype==FOFSTPROB) {
        if (opt.iverbose) {
        cout<<"FOFSTPROB which uses: ellphys, vratio, thetaopen, and ellthreshold.\n";
        cout<<"Parameters used are : ellphys="<<sqrt(param[6])<<" Lunits, vratio="<<param[7]<<", cos(thetaopen)="<<param[8]<<", ";
        cout<<param[9]<<" is outlier threshold.\n";
        }
        fofcmp=&FOFStreamwithprob;
    }
    else if (opt.foftype==FOF6DSUBSET) {
        if (opt.iverbose) {
        cout<<"FOF6D uses ellphys and ellvel.\n";
        cout<<"Parameters used are : ellphys="<<sqrt(param[6])<<" Lunits, ellvel="<<sqrt(param[7])<<" Vunits.\n";
        }
        fofcmp=&FOF6d;
    }
    else if (opt.foftype==FOFSTPROBNN) {
        if (opt.iverbose) {
        cout<<"FOFSTPROBNN which uses: ellphys, vratio, thetaopen, and ellthreshold.\n";
        cout<<"Parameters used are : ellphys="<<sqrt(param[6])<<" Lunits, vratio="<<param[7]<<", cos(thetaopen)="<<param[8]<<", ";
        cout<<param[9]<<" is outlier threshold.\n";
        cout<<"Search is limited to nearest neighbours."<<endl;
        }
        fofcmp=&FOFStreamwithprobNN;
    }
    else if (opt.foftype==FOFSTPROBNNLX) {
        if (opt.iverbose) {
        cout<<"FOFSTPROBNNLX which uses: ellphys, vratio, thetaopen, and ellthreshold.\n";
        cout<<"Parameters used are : ellphys="<<sqrt(param[6])<<" Lunits, vratio="<<param[7]<<", cos(thetaopen)="<<param[8]<<", ";
        cout<<param[9]<<" is outlier threshold.\n";
        cout<<"Search is limited to nearest neighbours."<<endl;
        }
        fofcmp=&FOFStreamwithprobNNLX;
    }
    else if (opt.foftype==FOFSTPROBNNNODIST) {
        if (opt.iverbose) {
        cout<<"FOFSTPROBNN which uses: vratio, thetaopen, and ellthreshold.\n";
        cout<<"Parameters used are : vratio="<<param[7]<<", cos(thetaopen)="<<param[8]<<", ";
        cout<<param[9]<<" is outlier threshold.\n";
        cout<<"Search is limited to nearest neighbours."<<endl;
        }
        fofcmp=&FOFStreamwithprobNNNODIST;
    }
    //@}
    //now actually search for dynamically distinct substructures 
    //@{
    if (!(opt.foftype==FOFSTPROBNN||opt.foftype==FOFSTPROBNNLX||opt.foftype==FOFSTPROBNNNODIST)) {
        if (opt.iverbose) cout<<"Building tree ... "<<endl;
        tree=new KDTree(Partsubset,nsubset,opt.Bsize,tree->TPHYS);
        param[0]=tree->GetTreeType();
        //if large enough for statistically significant structures to be found then search. This is a robust search
        if (nsubset>=MINSUBSIZE) {
            if (opt.iverbose) cout<<"Now search ... "<<endl;
            pfof=tree->FOFCriterion(fofcmp,param,numgroups,minsize,1,1,FOFchecksub);
        }
        else {
            numgroups=0;
            pfof=new Int_t[nsubset];
            for (i=0;i<nsubset;i++) pfof[i]=0;
        }
        if (opt.iverbose) cout<<"Done"<<endl;
    }
    else if (opt.foftype==FOFSTPROBNN||opt.foftype==FOFSTPROBNNLX||opt.foftype==FOFSTPROBNNNODIST) {
        //here idea is to use subset but only search NN neighbours in phase-space once that is built for each particle, look at first particle's NN and see if any meet stffof criteria for velocity
        //then examine first tagged particle that meets critera by examining its NN and so on till reach particle where all NN are either already tagged or do not meet criteria
        //delete tree;
        if (opt.iverbose) cout<<"Building tree ... "<<endl;
        tree=new KDTree(Partsubset,nsubset,opt.Bsize,tree->TPHYS,tree->KEPAN,1000,1);
        if (opt.iverbose) cout<<"Finding nearest neighbours"<<endl;
        nnID=new Int_t*[nsubset];
        for (i=0;i<nsubset;i++) nnID[i]=new Int_t[nsearch];
        dist2=new Double_t*[nthreads];
        for (int j=0;j<nthreads;j++)dist2[j]=new Double_t[nsearch];
#ifdef USEOPENMP
#pragma omp parallel default(shared) \
private(i,tid)
{
#pragma omp for
#endif
        for (i=0;i<nsubset;i++) {
#ifdef USEOPENMP
            tid=omp_get_thread_num();
#else
            tid=0;
#endif
            tree->FindNearest(i,nnID[i],dist2[tid],nsearch);
        }
#ifdef USEOPENMP
}
#endif
        if (opt.iverbose) cout<<"Done"<<endl;
        if (opt.iverbose) cout<<"search nearest neighbours"<<endl;
        pfof=tree->FOFNNCriterion(fofcmp,param,nsearch,nnID,numgroups,minsize);
        for (i=0;i<nsubset;i++) delete[] nnID[i];
        delete[] nnID;
        for (i=0;i<nthreads;i++) delete[] dist2[i];
        delete[] dist2;
        if (opt.iverbose) cout<<"Done"<<endl;
    }
    //@}

    //iteration to search region around streams using lower thresholds
    //determine number of groups
    if (opt.iiterflag&&numgroups>0) {
        Int_t ng=numgroups;
        int mergers;
        int *igflag,*ilflag;
        Int_t newlinks, intergrouplinks,*newlinksIndex,*intergrouplinksIndex;
        Int_t pid,ppid, ss,tail,startpoint;
        Int_t numgrouplinks,*numgrouplinksIndex,**newIndex,*oldnumingroup,**newintergroupIndex,**intergroupgidIndex;

        //to slowly expand search, must declare several arrays making it easier to move along a group. 
        numingroup=BuildNumInGroup(nsubset, numgroups, pfof);
        //here the lists are index order, not id order
        pglist=BuildPGList(nsubset, numgroups, numingroup, pfof, Partsubset);
        Head=BuildHeadArray(nsubset,numgroups,numingroup,pglist);
        Next=BuildNextArray(nsubset,numgroups,numingroup,pglist);
        GroupTail=BuildGroupTailArray(nsubset,numgroups,numingroup,pglist);
        newlinksIndex=new Int_t[nsubset];
        intergrouplinksIndex=new Int_t[numgroups+1];
        igflag=new int[numgroups+1];
        ilflag=new int[numgroups+1];
        for (i=1;i<=numgroups;i++) igflag[i]=0;
        /// NOTE for openmp, only significantly faster if nnID is 1 D array for reduction purposes
        /// However, this means that in this MPI domain, only nthreads < 2^32 / nsubset can be used since larger numbers require 64 bit array addressing
        /// This should not be an issue but a check is placed anyway
#ifdef USEOPENMP
#pragma omp parallel
        {
            if (omp_get_thread_num()==0) maxnthreads=omp_get_num_threads();
            if (omp_get_thread_num()==0) nthreads=omp_get_num_threads();
        }
        if (log((double)nthreads*nsubset)/log(2.0)>32.0) nthreads=(max(1,(int)(pow((double)2,32)/(double)nsubset)));
        omp_set_num_threads(nthreads);
#endif
        nnID=new Int_t*[1];
        nnID[0]=new Int_t[nsubset*nthreads];

        numgrouplinksIndex=new Int_t[numgroups+1];
        oldnumingroup=new Int_t[numgroups+1];
        newIndex=new Int_t*[numgroups+1];

        //first search groups near cell size as subhaloes near this scale may have central regions defining background and as such these particles will not appear to be 
        //distinct outliers (will probably have ell~1-2) thus resulting in an underestimate in the mass and maximum circular velocity for compact massive (sub)halos
        //this linking uses a smaller physical linking length and searchs already tagged particles for any untagged particles included in the larger subset made with the lower ellthreshold
        //so long as one particle lies about the high ell threshold, the particles can be check to see if they lie in the same phase-space.
        param[1]=(opt.ellxscale*opt.ellxscale)*(opt.ellphys*opt.ellphys);
        param[2]=(opt.ellvscale*opt.ellvscale)*(opt.ellvel*opt.ellvel);
        param[6]=(opt.ellxscale*opt.ellxscale)*(opt.ellphys*opt.ellphys);
        param[7]=(opt.Vratio);
        param[8]=cos(opt.thetaopen*M_PI);
        param[9]=opt.ellthreshold*opt.ellfac;
        fofcmp=&FOFStreamwithprobIterative;
        if (opt.iverbose) {
        cout<<ThisTask<<" "<<"Begin expanded search for groups near cell size"<<endl;
        cout<<ThisTask<<" "<<"Parameters used are : ellphys="<<sqrt(param[6])<<" Lunits, vratio="<<param[7]<<", cos(thetaopen)="<<param[8]<<", ";
        cout<<ThisTask<<" "<<param[9]<<" is outlier threshold.\n";
        }

        //first identify all particles to be searched
        newlinks=0;
        for (i=1;i<=numgroups;i++) if (numingroup[i]>opt.Ncell*0.1&&igflag[i]==0) 
        {
            ss=Head[pglist[i][0]];
            do {newlinksIndex[newlinks++]=ss;} while((ss = Next[ss]) >= 0);
        }
        //initialize nnID list so that if particle tagged no need to check comparison function
        for (ii=0;ii<nsubset;ii++) nnID[0][ii]=0;
        for (ii=0;ii<newlinks;ii++) nnID[0][Partsubset[newlinksIndex[ii]].GetID()]=pfof[Partsubset[newlinksIndex[ii]].GetID()];
        //first search list and find any new links, not that here if a particle has a pfof value > that the pfofvalue of the reference particle, its nnID value is set to the reference
        //particle's group id, otherwise, its left alone (unless its untagged)
        //if number of searches is large, run search in parallel
        SearchForNewLinks(nsubset, tree, Partsubset, pfof, fofcmp, param, newlinks, newlinksIndex, nnID, nthreads);
        tid=0;
        //determine groups
        //newIndex=DetermineNewLinks(nsubset, Partsubset, pfof, numgroups, newlinks, newlinksIndex, numgrouplinksIndex, nnID[tid]);
        DetermineNewLinks(nsubset, Partsubset, pfof, numgroups, newlinks, newlinksIndex, numgrouplinksIndex, nnID[tid],&newIndex);
        //link all previously unlinked particles for large groups by searching for each group the number of links for that group and linking them (if unlinked) 
        LinkUntagged(Partsubset, numgroups, pfof, numingroup, pglist, newlinks, numgrouplinksIndex, newIndex, Head, Next, GroupTail,nnID[tid]);
        for (Int_t j=1;j<=numgroups;j++) delete[] newIndex[j];

        for (i=0;i<nsubset;i++) if (Partsubset[i].GetPotential()<param[9]&&nnID[0][Partsubset[i].GetID()]==0) nnID[0][Partsubset[i].GetID()]=-1;

        fofcmp=&FOFStreamwithprob;
        param[1]=(opt.ellxscale*opt.ellxscale)*(opt.ellphys*opt.ellphys)*opt.ellxfac*opt.ellxfac;//increase physical linking length slightly
        param[6]=(opt.ellxscale*opt.ellxscale)*(opt.ellphys*opt.ellphys)*opt.ellxfac*opt.ellxfac;
        param[7]=(opt.Vratio)*opt.vfac;
        param[8]=cos(opt.thetaopen*M_PI*opt.thetafac);
        param[9]=opt.ellthreshold*opt.ellfac;
        if (opt.iverbose) {
        cout<<ThisTask<<" "<<"Begin second expanded search with large linking length "<<endl;
        cout<<ThisTask<<" "<<"FOFSTPROB which uses: ellphys, vratio, thetaopen, and ellthreshold.\n";
        cout<<ThisTask<<" "<<"Parameters used are : ellphys="<<sqrt(param[6])<<" Lunits, vratio="<<param[7]<<", cos(thetaopen)="<<param[8]<<", ";
        cout<<ThisTask<<" "<<param[9]<<" is outlier threshold.\n";
        }
        //initialization, for merging, store old size prior to expanded search, set nnID
        newlinks=0;
        for (i=1;i<=numgroups;i++) 
        {
            oldnumingroup[i]=numingroup[i];
            startpoint=Head[pglist[i][0]];
            ss=startpoint;
            do {if (Partsubset[ss].GetPotential()>=param[9]){newlinksIndex[newlinks++]=ss;}} while((ss = Next[ss]) >= 0);
        }
        //now continue to search all new links till there are no more links found
        tid=0;
        do {
            SearchForNewLinks(nsubset, tree, Partsubset, pfof, fofcmp, param, newlinks, newlinksIndex, nnID, nthreads);
            DetermineNewLinks(nsubset, Partsubset, pfof, numgroups, newlinks, newlinksIndex, numgrouplinksIndex, nnID[tid],&newIndex);
            LinkUntagged(Partsubset, numgroups, pfof, numingroup, pglist, newlinks, numgrouplinksIndex, newIndex, Head, Next, GroupTail,nnID[tid]);
            for (Int_t j=1;j<=numgroups;j++) delete[] newIndex[j];
        }while(newlinks);

        //then search for intergroup links. When merging, identify all intergroup links and define merging criterion based on mass ratios, number of links, old mass (prior to expansion), etc
        if (opt.iverbose) cout<<ThisTask<<" "<<"Search for intergroup links"<<endl;
        newintergroupIndex=new Int_t*[numgroups+1];
        intergroupgidIndex=new Int_t*[numgroups+1];
        newlinks=0;
        tid=0;
        for (i=1;i<=numgroups;i++) if (igflag[i]==0)
        {
            startpoint=Head[pglist[i][0]];
            ss=startpoint;
            do {if (Partsubset[ss].GetDensity()>=param[9]){newlinksIndex[newlinks++]=ss;}} while((ss = Next[ss]) >= 0);
        }
        for (ii=0;ii<newlinks;ii++) nnID[0][Partsubset[newlinksIndex[ii]].GetID()]=pfof[Partsubset[newlinksIndex[ii]].GetID()];
        do {
            //initialize
            mergers=0;
            //first search list and find any new links, not that here if a particle has a pfof value > that the pfofvalue of the reference particle, its nnID value is set to the reference
            //particle's group id, otherwise, its left alone (unless its untagged)
            //if number of searches is large, run search in parallel
            SearchForNewLinks(nsubset, tree, Partsubset, pfof, fofcmp, param, newlinks, newlinksIndex, nnID, nthreads);
            DetermineGroupLinks(nsubset, Partsubset, pfof, numgroups, newlinks, newlinksIndex, numgrouplinksIndex, nnID[tid], &newIndex);
            DetermineGroupMergerConnections(Partsubset, numgroups, pfof, ilflag, numgrouplinksIndex, intergrouplinksIndex, nnID[tid], &newIndex, &newintergroupIndex, &intergroupgidIndex);
            newlinks=0;
            mergers+=MergeGroups(opt, Partsubset, numgroups, pfof, numingroup, oldnumingroup, pglist, numgrouplinksIndex, &intergroupgidIndex, &newintergroupIndex, intergrouplinksIndex, Head, Next, GroupTail, igflag, nnID[tid], newlinks, newlinksIndex);
        }while(mergers);

        param[1]=(opt.ellxscale*opt.ellxscale)*(opt.ellphys*opt.ellphys)*opt.ellxfac*opt.ellxfac*2.25;//increase physical linking length slightly again
        param[6]=(opt.ellxscale*opt.ellxscale)*(opt.ellphys*opt.ellphys)*opt.ellxfac*opt.ellxfac*2.25;
        param[7]=(opt.Vratio)*opt.vfac;
        param[8]=cos(opt.thetaopen*M_PI*opt.thetafac);
        param[9]=opt.ellthreshold*opt.ellfac;
        if (opt.iverbose){
        cout<<ThisTask<<" "<<"Begin second expanded search with large linking length "<<endl;
        cout<<ThisTask<<" "<<"FOFSTPROB which uses: ellphys, vratio, thetaopen, and ellthreshold.\n";
        cout<<ThisTask<<" "<<"Parameters used are : ellphys="<<sqrt(param[6])<<" Lunits, vratio="<<param[7]<<", cos(thetaopen)="<<param[8]<<", ";
        cout<<ThisTask<<" "<<param[9]<<" is outlier threshold."<<endl;
        }
        //initialization, for merging, store old size prior to expanded search, set nnID
        newlinks=0;
        for (i=1;i<=numgroups;i++) if (igflag[i]==0) 
        {
            startpoint=Head[pglist[i][0]];
            ss=startpoint;
            do {if (Partsubset[ss].GetPotential()>=param[9]){newlinksIndex[newlinks++]=ss;}} while((ss = Next[ss]) >= 0);
        }
        //now continue to search all new links till there are no more links found
        tid=0;
        do {
            SearchForNewLinks(nsubset, tree, Partsubset, pfof, fofcmp, param, newlinks, newlinksIndex, nnID, nthreads);
            DetermineNewLinks(nsubset, Partsubset, pfof, numgroups, newlinks, newlinksIndex, numgrouplinksIndex, nnID[tid],&newIndex);
            LinkUntagged(Partsubset, numgroups, pfof, numingroup, pglist, newlinks, numgrouplinksIndex, newIndex, Head, Next, GroupTail,nnID[tid]);
            for (Int_t j=1;j<=numgroups;j++) delete[] newIndex[j];
        }while(newlinks);

        //release memory
        for (i=1;i<=numgroups;i++)delete[] pglist[i];
        delete[] Head;
        delete[] Next;
        delete[] GroupTail;
        delete[] newlinksIndex;
        delete[] numgrouplinksIndex;
        delete[] newIndex;
        delete[] igflag;
        delete[] nnID[0];
        delete[] nnID;
        delete[] intergrouplinksIndex;
        delete[] intergroupgidIndex;
        delete[] newintergroupIndex;
        delete[] ilflag;

#ifdef USEOPENMP
#pragma omp parallel
    {
    omp_set_num_threads(maxnthreads);
    }
    nthreads=maxnthreads;
#endif
        //adjust groups
        for (i=1;i<=numgroups;i++) numingroup[i]=0;
        for (i=0;i<nsubset;i++) numingroup[pfof[i]]++;
        for (i=0;i<nsubset;i++) if (numingroup[pfof[i]]<opt.MinSize) pfof[i]=0;
        for (i=1;i<=numgroups;i++) numingroup[i]=0;
        for (i=0;i<nsubset;i++) numingroup[pfof[i]]++;
        //cout<<ThisTask<<" "<<"Now determine number of groups with non zero length"<<endl;
        for (i=numgroups;i>=1;i--) {
            if (numingroup[i]==0) ng--;
            else pglist[i]=new Int_t[numingroup[i]];
            numingroup[i]=0;
        }
        for (i=0;i<nsubset;i++) if (pfof[i]>0) pglist[pfof[i]][numingroup[pfof[i]]++]=i;
        if (ng) ReorderGroupIDs(numgroups, ng, numingroup, pfof, pglist);
        for (i=1;i<=numgroups;i++) if (numingroup[i]>0) delete[] pglist[i];
        delete[] pglist;
        delete[] numingroup;
        numgroups=ng;
        if (opt.iverbose==2) cout<<ThisTask<<" "<<"After expanded search there are now "<< ng<<" groups"<<endl;
    }
    //once substructure groups are found, ensure all substructure groups are significant
    if (numgroups) 
    {
        numingroup=BuildNumInGroup(nsubset, numgroups, pfof);
        //pglist is constructed without assuming particles are in index order
        pglist=BuildPGList(nsubset, numgroups, numingroup, pfof, Partsubset);
        CheckSignificance(opt,nsubset,Partsubset,numgroups,numingroup,pfof,pglist);
        for (i=1;i<=numgroups;i++) delete[] pglist[i];
        delete[] pglist;
        delete[] numingroup;
    }
    if (numgroups>0) if (opt.iverbose==2) cout<<ThisTask<<": "<<numgroups<<" substructures found"<<endl;
    else if (opt.iverbose==2) cout<<ThisTask<<": "<<"NO SUBSTRUCTURES FOUND"<<endl;

    //now search particle list for large compact substructures that are considered part of the background when using smaller grids
    //if smaller substructures have been found, also search for true 6d cores for signs of similar mass mergers
    //if (nsubset>opt.HaloMergerSize&&((!opt.iSingleHalo&&sublevel==1)||(opt.iSingleHalo&&sublevel==0)))
    if (nsubset>MINSUBSIZE)
    {
        //first have to delete tree used in search so that particles are in original particle order
        //then construct a new grid with much larger cells so that new bg velocity dispersion can be estimated
        delete tree;
        Int_t ngrid;
        Coordinate *gvel;
        Matrix *gveldisp;
        GridCell *grid;
        Double_t nf, ncl=opt.Ncell;
        //adjust ncellfac locally
        nf=(opt.Ncellfac*8.0,0.1);
        opt.Ncell=nf*nsubset;

        //ONLY calculate grid quantities if substructures have been found 
        if (numgroups>0) {
            tree=InitializeTreeGrid(opt,nsubset,Partsubset);
            ngrid=tree->GetNumLeafNodes();
            if (opt.iverbose) cout<<ThisTask<<" "<<"bg search using "<<ngrid<<" grid cells, with each node containing ~"<<(opt.Ncell=nsubset/ngrid)<<" particles"<<endl;
            grid=new GridCell[ngrid];
            FillTreeGrid(opt, nsubset, ngrid, tree, Partsubset, grid);
            gvel=GetCellVel(opt,nsubset,Partsubset,ngrid,grid);
            gveldisp=GetCellVelDisp(opt,nsubset,Partsubset,ngrid,grid,gvel);
            GetDenVRatio(opt,nsubset,Partsubset,ngrid,grid,gvel,gveldisp);
            GetOutliersValues(opt,nsubset,Partsubset,-1);
        }
        ///produce tree to search for 6d phase space structures 
        tree=new KDTree(Partsubset,nsubset,opt.Bsize,tree->TPHYS);

        //now begin fof6d search for large background objects that are missed using smaller grid cells ONLY IF substructures have been found
        if (numgroups>0) {
            bgoffset=0;
            minsize=ncl*0.2;
            //use same linking length to find substructures
            param[1]=(opt.ellxscale*opt.ellxscale)*(opt.ellphys*opt.ellphys)*(opt.ellxfac*opt.ellxfac);
            param[6]=param[1];
            //velocity linking length from average sigmav from grid
            param[7]=opt.HaloSigmaV;
            param[8]=cos(opt.thetaopen*M_PI);
            param[9]=opt.ellthreshold*opt.ellfac*0.8;//since this is the background, threshold is upper limit
            if (opt.iverbose) {
            cout<<ThisTask<<" "<<"FOF6D uses ellphys and ellvel."<<endl;
            cout<<ThisTask<<" "<<"Parameters used are : ellphys="<<sqrt(param[6])<<" Lunits, ellvel="<<sqrt(param[7])<<" Vunits."<<endl;
            cout<<ThisTask<<" "<<param[9]<<" is outlier threshold and contains more than "<<minsize<<endl;
            }
            //then reset to find large subhalo cores close to cell size limit
            fofcmp=&FOF6dbgup;
            //here this ensures that particles belong to a 6dfof substructure that is composed of particles
            //which are considered dynamical outliers using a very large grid
            pfofbg=tree->FOFCriterion(fofcmp,param,numgroupsbg,minsize,1,1,FOFchecksub);

            //now combine results such that if particle already belongs to a substructure ignore
            //otherwise leave tagged. Then check if these new background substructures share enough links with the
            //other substructures that they should be joined.
            if (numgroupsbg>=bgoffset+1) {
                int mergers;
                int *igflag,*ilflag;
                Int_t newlinks, oldlinks,intergrouplinks,*newlinksIndex,*oldlinksIndex,*intergrouplinksIndex;
                Int_t pid,ppid, ss,tail,startpoint;
                Int_t numgrouplinks,*numgrouplinksIndex,**newIndex,**newintergroupIndex,**intergroupgidIndex;

                Int_t ng=numgroups+(numgroupsbg-bgoffset),oldng=numgroups;
                for (i=0;i<nsubset;i++) if (pfof[i]==bgoffset&&pfofbg[i]>0) pfof[i]=oldng+(pfofbg[i]-bgoffset);
                numgroups+=(numgroupsbg-bgoffset);
                if (opt.iverbose) cout<<ThisTask<<" "<<"Found "<<numgroups<<endl;

                //
                numingroup=BuildNumInGroup(nsubset, numgroups, pfof);
                pglist=BuildPGList(nsubset, numgroups, numingroup, pfof, Partsubset);
                Head=BuildHeadArray(nsubset,numgroups,numingroup,pglist);
                Next=BuildNextArray(nsubset,numgroups,numingroup,pglist);
                GroupTail=BuildGroupTailArray(nsubset,numgroups,numingroup,pglist);
                newlinksIndex=new Int_t[nsubset];
                oldlinksIndex=new Int_t[nsubset];
                intergrouplinksIndex=new Int_t[numgroups+1];
                igflag=new int[numgroups+1];
                ilflag=new int[numgroups+1];
                for (i=1;i<=numgroups;i++) igflag[i]=0;
#ifdef USEOPENMP
#pragma omp parallel
                {
                    if (omp_get_thread_num()==0) maxnthreads=omp_get_num_threads();
                    if (omp_get_thread_num()==0) nthreads=omp_get_num_threads();
                }
                if (log((double)nthreads*nsubset)/log(2.0)>32.0) nthreads=(max(1,(int)(pow((double)2,32)/(double)nsubset)));
                omp_set_num_threads(nthreads);
#endif

                nnID=new Int_t*[1];
                nnID[0]=new Int_t[nsubset*nthreads];

                numgrouplinksIndex=new Int_t[numgroups+1];
                newIndex=new Int_t*[numgroups+1];

                //initialize nnID list so that if particle tagged no need to check comparison function
                for (ii=0;ii<nsubset;ii++) nnID[0][ii]=0;
                if (opt.iverbose) cout<<ThisTask<<" "<<"Search for bg intergroup links"<<endl;
                newintergroupIndex=new Int_t*[numgroups+1];
                intergroupgidIndex=new Int_t*[numgroups+1];
                oldlinks=0;
                tid=0;
                for (i=1;i<=oldng;i++) {
                    startpoint=Head[pglist[i][0]];
                    ss=startpoint;
                    do {{oldlinksIndex[oldlinks++]=ss;}} while((ss = Next[ss]) >= 0);
                }
                for (ii=0;ii<oldlinks;ii++)
                    nnID[0][Partsubset[oldlinksIndex[ii]].GetID()]=pfof[Partsubset[oldlinksIndex[ii]].GetID()]+numgroups;
                newlinks=0;
                for (i=oldng+1;i<=numgroups;i++) if (numingroup[i]>0) {
                    startpoint=Head[pglist[i][0]];
                    ss=startpoint;
                    do {{newlinksIndex[newlinks++]=ss;}} while((ss = Next[ss]) >= 0);
                }
                else igflag[i]=1;
                for (ii=0;ii<newlinks;ii++)
                    nnID[0][Partsubset[newlinksIndex[ii]].GetID()]=pfof[Partsubset[newlinksIndex[ii]].GetID()];

                //now begin interative 
                param[1]=(opt.ellxscale*opt.ellxscale)*(opt.ellphys*opt.ellphys);
                param[2]=(opt.ellvscale*opt.ellvscale)*(opt.ellvel*opt.ellvel);
                param[6]=(opt.ellxscale*opt.ellxscale)*(opt.ellphys*opt.ellphys);
                param[7]=(opt.Vratio);
                param[8]=cos(opt.thetaopen*M_PI);
                param[9]=opt.ellthreshold*opt.ellfac*0.8;
                fofcmp=&FOFStreamwithprobIterative;
                if (opt.iverbose){
                cout<<ThisTask<<" "<<"Begin expanded search for groups near cell size"<<endl;
                cout<<ThisTask<<" "<<"Parameters used are : ellphys="<<sqrt(param[6])<<" Lunits, vratio="<<param[7]<<", cos(thetaopen)="<<param[8]<<", ";
                cout<<ThisTask<<" "<<param[9]<<" is outlier threshold.\n";
                }
                SearchForNewLinks(nsubset, tree, Partsubset, pfof, fofcmp, param, newlinks, newlinksIndex, nnID, nthreads);
                tid=0;
                DetermineNewLinks(nsubset, Partsubset, pfof, numgroups, newlinks, newlinksIndex, numgrouplinksIndex, nnID[tid],&newIndex);
                LinkUntagged(Partsubset, numgroups, pfof, numingroup, pglist, newlinks, numgrouplinksIndex, newIndex, Head, Next, GroupTail,nnID[tid]);
                for (Int_t j=1;j<=numgroups;j++) delete[] newIndex[j];
                newlinks=0;
                for (i=oldng+1;i<=numgroups;i++) if (numingroup[i]>0) {
                    startpoint=Head[pglist[i][0]];
                    ss=startpoint;
                    do {{newlinksIndex[newlinks++]=ss;}} while((ss = Next[ss]) >= 0);
                }
                else igflag[i]=1;

                fofcmp=&FOFStreamwithprob;
                param[1]=(opt.ellxscale*opt.ellxscale)*(opt.ellphys*opt.ellphys)*opt.ellxfac*opt.ellxfac;//increase physical linking length slightly
                param[6]=(opt.ellxscale*opt.ellxscale)*(opt.ellphys*opt.ellphys)*opt.ellxfac*opt.ellxfac;
                param[7]=(opt.Vratio)*opt.vfac;
                param[8]=cos(opt.thetaopen*M_PI*opt.thetafac);
                param[9]=-3.0;

                mergers=0;
                SearchForNewLinks(nsubset, tree, Partsubset, pfof, fofcmp, param, newlinks, newlinksIndex, nnID, nthreads);
                for (ii=0;ii<oldlinks;ii++)
                    if (nnID[0][Partsubset[oldlinksIndex[ii]].GetID()]==pfof[Partsubset[oldlinksIndex[ii]].GetID()]+numgroups)
                        nnID[0][Partsubset[oldlinksIndex[ii]].GetID()]-=numgroups;
                DetermineGroupLinks(nsubset, Partsubset, pfof, numgroups, oldlinks, oldlinksIndex, numgrouplinksIndex, nnID[tid], &newIndex);
                DetermineGroupMergerConnections(Partsubset, numgroups, pfof, ilflag, numgrouplinksIndex, intergrouplinksIndex, nnID[tid], &newIndex, &newintergroupIndex, &intergroupgidIndex);
                mergers+=MergeGroups(opt, Partsubset, numgroups, pfof, numingroup, numingroup, pglist, numgrouplinksIndex, &intergroupgidIndex, &newintergroupIndex, intergrouplinksIndex, Head, Next, GroupTail, igflag, nnID[tid], newlinks, newlinksIndex);

                //release memory
                for (i=1;i<=numgroups;i++)delete[] pglist[i];
                delete[] Head;
                delete[] Next;
                delete[] GroupTail;
                delete[] newlinksIndex;
                delete[] numgrouplinksIndex;
                delete[] newIndex;
                delete[] igflag;
                delete[] nnID[0];
                delete[] nnID;
                delete[] intergrouplinksIndex;
                delete[] intergroupgidIndex;
                delete[] newintergroupIndex;
                delete[] ilflag;

#ifdef USEOPENMP
#pragma omp parallel
    {
    omp_set_num_threads(maxnthreads);
    }
    nthreads=maxnthreads;
#endif

                //adjust sizes and reorder groups
                for (i=1;i<=numgroups;i++) numingroup[i]=0;
                for (i=0;i<nsubset;i++) numingroup[pfof[i]]++;
                for (i=0;i<nsubset;i++) if (numingroup[pfof[i]]<opt.MinSize) pfof[i]=0;
                for (i=1;i<=numgroups;i++) numingroup[i]=0;
                for (i=0;i<nsubset;i++) numingroup[pfof[i]]++;
                if (opt.iverbose) cout<<ThisTask<<" "<<"Now determine number of groups with non zero length"<<endl;
                for (i=numgroups;i>=1;i--) {
                    if (numingroup[i]==0) ng--;
                    else pglist[i]=new Int_t[numingroup[i]];
                    numingroup[i]=0;
                }
                for (i=0;i<nsubset;i++) if (pfof[i]>0) pglist[pfof[i]][numingroup[pfof[i]]++]=i;
                if (ng) ReorderGroupIDs(numgroups, ng, numingroup, pfof, pglist);
                for (i=1;i<=numgroups;i++) if (numingroup[i]>0) delete[] pglist[i];
                delete[] pglist;
                delete[] numingroup;
                numgroups=ng;
                if (opt.iverbose) cout<<ThisTask<<" "<<"After expanded search there are now "<< ng<<" groups"<<endl;
            }
            else if (opt.iverbose) cout<<ThisTask<<" "<<"No large background substructure groups found"<<endl;
            delete[] pfofbg;
        }
        //output results of search
        if (numgroups>0) if (opt.iverbose==2) cout<<numgroups<<" substructures found after large grid search"<<endl;
        else if (opt.iverbose==2) cout<<"NO SUBSTRUCTURES FOUND"<<endl;
    }
    //ONCE ALL substructures are found, search for cores of major mergers with minimum size set by cell size since grid is quite large after bg search
    //for missing large substructure cores
    if(opt.iHaloCoreSearch>0&&((!opt.iSingleHalo&&sublevel==1)||(opt.iSingleHalo&&sublevel==0))) 
    {
        bgoffset=1;
        if (opt.iverbose) cout<<ThisTask<<" beginning 6dfof core search to find multiple cores"<<endl;
        param[1]=(opt.ellxscale*opt.ellphys*opt.ellhalophysfac*opt.halocorexfac);
        param[1]=param[1]*param[1];
        param[6]=param[1];
        //velocity linking length from average sigmav from FINE SCALE grid
        param[7]=opt.HaloSigmaV*(opt.halocorevfac*opt.halocorevfac);
        minsize=nsubset*opt.halocorenfac;
        if (opt.iverbose) {
        cout<<ThisTask<<" "<<"Parameters used are : ellphys="<<sqrt(param[6])<<" Lunits, ellphys="<<sqrt(param[7])<<" Vunits"<<endl;
        cout<<"with minimum size of "<<minsize<<endl;
        }
        fofcmp=&FOF6d;
        //here search for 6dfof groups, return ordered list. Also pass function check to ignore already tagged particles
        int iorder=1,icheck=1;
        //for ignoring already tagged particles can just use the oultier potential and FOFcheckbg 
        for (i=0;i<nsubset;i++) Partsubset[i].SetPotential(pfof[Partsubset[i].GetID()]);
        param[9]=0.5;
        pfofbg=tree->FOFCriterion(fofcmp,param,numgroupsbg,minsize,iorder,icheck,FOFcheckbg);
        if (numgroupsbg>=bgoffset+1) {
            if (opt.iverbose) cout<<"Number of cores: "<<numgroupsbg<<endl;
            //if cores are found, two options
            //a pnumcores pointer is passed, store the number of cores in this pointer assumes halo ids will not be reordered, making it easy to flag cores
            //if opt.iHaloCoreSearch==2 then start searching the halo to identify which particles belong to which core by linking particles to their closest
            //tagged neighbour in phase-space.
            Int_t ng=numgroups+(numgroupsbg-bgoffset),oldng=numgroups;
            if(pnumcores!=NULL) *pnumcores=numgroupsbg;
            if (opt.iHaloCoreSearch==2) {
                //for simplicity make a new particle array storing core particles
                Int_t nincore=0,nbucket=opt.Bsize,pid, pidcore;
                Particle *Pcore,*Pval;
                KDTree *tcore;
                Coordinate x1;
                Double_t D2,dval,mval;
                Double_t *mcore=new Double_t[numgroupsbg+1];
                for (i=0;i<=numgroupsbg;i++)mcore[i]=0;
                int nsearch=opt.Nvel;
                for (i=0;i<nsubset;i++) {
                    if (pfofbg[i]>0) {
                        nincore++;
                        mcore[pfofbg[i]]++;
                    }
                }
                if (opt.iverbose) {
                    cout<<"Mass ratios of cores are "<<endl;
                    for (i=1;i<=numgroupsbg;i++)cout<<i<<" "<<mcore[i]<<" "<<mcore[i]/mcore[1]<<endl;
                }
                if (nincore<nsubset) {
                if (nsearch>nincore) nsearch=nincore;
                if (nbucket>=nincore/8) nbucket=max(1,(int)nincore/8);
                Pcore=new Particle[nincore];
                nincore=0;
                for (i=0;i<nsubset;i++) if (pfofbg[Partsubset[i].GetID()]>0) {
                    Pcore[nincore]=Partsubset[i];
                    Pcore[nincore].SetType(pfofbg[Partsubset[i].GetID()]);
                    nincore++;
                }
                tcore=new KDTree(Pcore,nincore,opt.Bsize,tree->TPHYS);
                nnID=new Int_t*[nthreads];
                dist2=new Double_t*[nthreads];
                for (i=0;i<nthreads;i++) {
                    nnID[i]=new Int_t[nsearch];
                    dist2[i]=new Double_t[nsearch];
                }
                if (opt.iverbose) cout<<"Searching untagged particles to assign to cores "<<endl;
#ifdef USEOPENMP
                if (nsubset>ompperiodnum) {
#pragma omp parallel default(shared) \
private(i,tid,Pval,x1,D2,dval,mval,pid,pidcore)
{
#pragma omp for
                for (i=0;i<nsubset;i++) 
                {
                    tid=omp_get_thread_num();
                    Pval=&Partsubset[i];
                    pid=Pval->GetID();
                    if (pfofbg[pid]==0 && pfof[pid]==0) {
                        x1=Coordinate(Pval->GetPosition());
                        tcore->FindNearestPos(x1, nnID[tid], dist2[tid],nsearch);
                        dval=0;
                        pidcore=nnID[tid][0];
                        for (int k=0;k<3;k++) {
                            dval+=(Pval->GetPosition(k)-Pcore[pidcore].GetPosition(k))*(Pval->GetPosition(k)-Pcore[pidcore].GetPosition(k))/param[6]+(Pval->GetVelocity(k)-Pcore[pidcore].GetVelocity(k))*(Pval->GetVelocity(k)-Pcore[pidcore].GetVelocity(k))/param[7];
                        }
                        mval=mcore[Pcore[pidcore].GetType()];
                        pfofbg[pid]=Pcore[pidcore].GetType();
                        for (int j=1;j<nsearch;j++) {
                            D2=0;
                            pidcore=nnID[tid][j];
                            for (int k=0;k<3;k++) {
                                D2+=(Pval->GetPosition(k)-Pcore[pidcore].GetPosition(k))*(Pval->GetPosition(k)-Pcore[pidcore].GetPosition(k))/param[6]+(Pval->GetVelocity(k)-Pcore[pidcore].GetVelocity(k))*(Pval->GetVelocity(k)-Pcore[pidcore].GetVelocity(k))/param[7];
                            }
                            if (dval>D2*mval/mcore[Pcore[pidcore].GetType()]) {dval=D2;mval=mcore[Pcore[pidcore].GetType()];pfofbg[pid]=Pcore[pidcore].GetType();}
                        }
                    }
                }
}
                }
                else {
#endif
                for (i=0;i<nsubset;i++) 
                {
                    tid=0;
                    Pval=&Partsubset[i];
                    pid=Pval->GetID();
                    if (pfofbg[pid]==0 && pfof[pid]==0) {
                        x1=Coordinate(Pval->GetPosition());
                        tcore->FindNearestPos(x1, nnID[tid], dist2[tid],nsearch);
                        dval=0;
                        pidcore=nnID[tid][0];
                        for (int k=0;k<3;k++) {
                            dval+=(Pval->GetPosition(k)-Pcore[pidcore].GetPosition(k))*(Pval->GetPosition(k)-Pcore[pidcore].GetPosition(k))/param[6]+(Pval->GetVelocity(k)-Pcore[pidcore].GetVelocity(k))*(Pval->GetVelocity(k)-Pcore[pidcore].GetVelocity(k))/param[7];
                        }
                        mval=mcore[Pcore[pidcore].GetType()];
                        pfofbg[pid]=Pcore[pidcore].GetType();
                        for (int j=1;j<nsearch;j++) {
                            D2=0;
                            for (int k=0;k<3;k++) {
                                D2+=(Pval->GetPosition(k)-Pcore[pidcore].GetPosition(k))*(Pval->GetPosition(k)-Pcore[pidcore].GetPosition(k))/param[6]+(Pval->GetVelocity(k)-Pcore[pidcore].GetVelocity(k))*(Pval->GetVelocity(k)-Pcore[pidcore].GetVelocity(k))/param[7];
                            }
                            if (dval>D2*mval/mcore[Pcore[pidcore].GetType()]) {dval=D2;mval=mcore[Pcore[pidcore].GetType()];pfofbg[pid]=Pcore[pidcore].GetType();}
                        }
                    }
                }
#ifdef USEOPENMP
                }
#endif

                delete tcore;
                delete[] Pcore;
                for (i=0;i<nthreads;i++) {
                    delete [] nnID[i];
                    delete [] dist2[i];
                }
                delete[] nnID;
                delete[] dist2;
                }
                delete[] mcore;
            }
            for (i=0;i<nsubset;i++) if (pfofbg[i]>bgoffset) pfof[i]=oldng+(pfofbg[i]-bgoffset);
            if (opt.iverbose) cout<<ThisTask<<": After 6dfof core search and assignment there are "<< ng<<" groups"<<endl;
            numgroups=ng;
        }
        else if (opt.iverbose) cout<<ThisTask<<": has found no excess cores indicating mergers"<<endl;
        delete[] pfofbg;
    }

#ifdef USEMPI
    //now if substructures are subsubstructures, then the region of interest has already been localized to a single MPI domain 
    //so do not need to search other mpi domains
    if (sublevel==0) {

    //if using MPI must determine which local particles need to be exported to other threads and used to search
    //that threads particles. This is done by seeing if the any particles have a search radius that overlaps with 
    //the boundaries of another threads domain. Then once have exported particles must search local particles
    //relative to these exported particles. Iterate over search till no new links are found.

    //First have barrier to ensure that all mpi tasks have finished the local search
    MPI_Barrier(MPI_COMM_WORLD);

    //To make it easy to search the local particle arrays, build several arrays, in particular, Head, Next, Len arrays
    Int_t *Len;
    numingroup=BuildNumInGroup(nsubset, numgroups, pfof);
    pglist=BuildPGList(nsubset, numgroups, numingroup, pfof,Partsubset);
    Len=BuildLenArray(nsubset,numgroups,numingroup,pglist);
    Head=BuildHeadArray(nsubset,numgroups,numingroup,pglist);
    Next=BuildNextArray(nsubset,numgroups,numingroup,pglist);
    for (i=1;i<=numgroups;i++)delete[] pglist[i];
    delete[] pglist;
    //Also must ensure that group ids do not overlap between mpi threads so adjust group ids
    MPI_Allgather(&numgroups, 1, MPI_Int_t, mpi_ngroups, 1, MPI_Int_t, MPI_COMM_WORLD);
    MPIAdjustLocalGroupIDs(nsubset, pfof);

    //then determine export particles, declare arrays used to export data
#ifdef MPIREDUCEMEM
    MPIGetExportNum(nbodies, Partsubset, sqrt(param[1]));
#endif
    //then determine export particles, declare arrays used to export data
    PartDataIn = new Particle[NExport];
    PartDataGet = new Particle[NExport];
    FoFDataIn = new fofdata_in[NExport];
    FoFDataGet = new fofdata_in[NExport];
    //I have adjusted FOF data structure to have local group length and also seperated the export particles from export fof data
    //the reason is that will have to update fof data in iterative section but don't need to update particle information.
    MPIBuildParticleExportList(nsubset, Partsubset, pfof, Len, sqrt(param[1]));
    //Now that have FoFDataGet (the exported particles) must search local volume using said particles
    //This is done by finding all particles in the search volume and then checking if those particles meet the FoF criterion
    //One must keep iterating till there are no new links. 
    //Wonder if i don't need another loop and a final check
    Int_t links_across,links_across_total;
    do {
        links_across=MPILinkAcross(nsubset, tree, Partsubset, pfof, Len, Head, Next, param[1], fofcmp, param);
        MPIUpdateExportList(nsubset, Partsubset,pfof,Len);
        MPI_Allreduce(&links_across, &links_across_total, 1, MPI_Int_t, MPI_SUM, MPI_COMM_WORLD);
    }while(links_across_total>0);

    //reorder local particle array and delete memory associated with Head arrays, only need to keep Particles, pfof and some id and idexing information
    delete tree;
    delete[] Head;
    delete[] Next;
    delete[] Len;
    delete[] numingroup;
    delete[] FoFDataIn;
    delete[] FoFDataGet;
    delete[] PartDataIn;
    delete[] PartDataGet;

    //Now redistribute groups so that they are local to a processor (also orders the group ids according to size
    if (opt.iSingleHalo) opt.MinSize=MinNumOld;//reset minimum size
    Int_t newnbodies=MPIGroupExchange(nsubset,Partsubset,pfof);
#ifndef MPIREDUCEMEM
    //once groups are local, can free up memory
    delete[] Partsubset;
    delete[] pfof;
    //delete[] mpi_idlist;//since particles have now moved, must generate new list
    //mpi_idlist=new Int_t[newnbodies];
    pfof=new Int_t[newnbodies];
    //store new particle data in mpi_Part1 as external variable ensures memory allocated is not deallocated when function returns
    mpi_Part1=new Particle[newnbodies];
    Partsubset=mpi_Part1;
#endif
    
    ///\todo Before final compilation of data, should have unbind here but must adjust unbind so it 
    ///does not call reordergroupids in it though it might be okay.
    //And compile the information and remove groups smaller than minsize
    numgroups=MPICompileGroups(newnbodies,Partsubset,pfof,opt.MinSize);
    MPI_Barrier(MPI_COMM_WORLD);
    cout<<"MPI thread "<<ThisTask<<" has found "<<numgroups<<endl;
    //free up memory now that only need to store pfof and global ids
    if (ThisTask==0) {
        int totalgroups=0;
        for (int j=0;j<NProcs;j++) totalgroups+=mpi_ngroups[j];
        cout<<"Total number of groups found is "<<totalgroups<<endl;
    }
    //free up memory now that only need to store pfof and global ids
    delete[] FoFGroupDataLocal;
    delete[] FoFGroupDataExport;
    Nlocal=newnbodies;
    }
    else{
#endif
        delete tree;
#ifdef USEMPI
    }
#endif
    cout<<"Done"<<endl;
    return pfof;
}

/*!
    Given a initial ordered candidate list of substructures, find all substructures that are large enough to be searched. 
    These substructures are used as a mean background velocity field and a new outlier list is found and searched.
    The subsubstructures must also be significant and if unbinding, must also be bound. If a particle in a subsubstructure is 
    removed from the subsubstructure, its fof id is set to that of the substructure it is contained in.
    
    The minimum size is given by \ref MINSUBSIZE. This should be for objects composed of ~1000 particles or so as realistically, 
    estimating the mean field needs at least 8 cells and ideally want about 100 particles per cell. Smaller objects will have 
    cells used in mean field estimates that span too large a region in the phase-space of the object. The poorly resolved halos 
    also have pretty noisy internal structures though they may have substructures composed of ~<10% of the particles (more
    realistially is ~<1%. So for 1000 that gives a maximum size of 100 particles. 
    
    NOTE: if the code is altered and generalized to outliers in say the entropy distribution when searching for gas shocks, 
    it might be possible to lower the cuts imposed. 
    
    \todo To account for major mergers, the mininmum size of object searched for substructure is now the smallest allowed cell 
    \ref MINCELLSIZE (order 100 particles). However, for objects smaller than \ref MINSUBSIZE, only can search effectively for 
    major mergers, very hard to identify substructures
*/
void SearchSubSub(Options &opt, const Int_t nsubset, Particle *&Partsubset, Int_t *&pfof, Int_t &ngroup, Int_t &nhalos)
{
    //now build a sublist of groups to search for substructure
    Int_t nsubsearch, oldnsubsearch,sublevel,maxsublevel,ngroupidoffset,ngroupidoffsetold,ngrid;
    bool iflag;
    Particle *subPart;
    Int_t ng,*numingroup,**pglist;
    Int_t *subpfof,*subngroup;
    Int_t *subnumingroup,**subpglist;
    Int_t **subsubnumingroup, ***subsubpglist;
    Int_t *numcores;
    Coordinate *gvel;
    Matrix *gveldisp;
    KDTree *tree;
    GridCell *grid;
    Coordinate cm,cmvel;
    //variables to keep track of structure level, pfof values (ie group ids) and their parent structure
    //use to point to current level
    StrucLevelData *pcsld;
    //use to store total number in sublevel;
    Int_t ns;
#ifndef USEMPI
    int ThisTask=0,NProcs=1;
#endif
    cout<<ThisTask<<" Beginning substructure search "<<endl;
    if (ngroup>0) {
    //point to current structure level 
    pcsld=psldata;
    //ns=0;

    //if a single halo was not passed then store fof halos for unbinding purposes
    if (!opt.iSingleHalo) nhalos=ngroup;

    nsubsearch=ngroup;sublevel=1;ngroupidoffset=ngroupidoffsetold=0;
    if (opt.iBaryonSearch==1 && opt.partsearchtype==PSTALL) numingroup=BuildNumInGroupTyped(nsubset, ngroup, pfof, Partsubset, DARKTYPE);
    else numingroup=BuildNumInGroup(nsubset, ngroup, pfof);
    //since initially groups in order find index of smallest group that can be searched for substructure
    //for (Int_t i=1;i<=ngroup;i++) if (numingroup[i]<MINSUBSIZE) {nsubsearch=i-1;break;}
    for (Int_t i=1;i<=ngroup;i++) if (numingroup[i]<MINCELLSIZE) {nsubsearch=i-1;break;}
    iflag=(nsubsearch>0);

    if (iflag) {
    if (opt.iBaryonSearch==1 && opt.partsearchtype==PSTALL) pglist=BuildPGListTyped(nsubset, ngroup, numingroup, pfof,Partsubset,DARKTYPE);
    else pglist=BuildPGList(nsubset, ngroup, numingroup, pfof);
    //now store group ids of (sub)structures that will be searched for (sub)substructure. 
    //since at level zero, the particle group list that is going to be used to calculate the background, outliers and searched through is simple pglist here
    //also the group size is simple numingroup
    subnumingroup=new Int_t[nsubsearch+1];
    subpglist=new Int_t*[nsubsearch+1];
    for (Int_t i=1;i<=nsubsearch;i++) {
        subnumingroup[i]=numingroup[i];
        subpglist[i]=new Int_t[subnumingroup[i]];
        for (Int_t j=0;j<subnumingroup[i];j++) subpglist[i][j]=pglist[i][j];
    }
    for (Int_t i=1;i<=ngroup;i++) delete[] pglist[i];
    delete[] pglist;
    delete[] numingroup;
    //now start searching while there are still sublevels to be searched

    while (iflag) {
        if (opt.iverbose) cout<<ThisTask<<" There are "<<nsubsearch<<" substructures large enough to search for other substructures at sub level "<<sublevel<<endl;
        oldnsubsearch=nsubsearch;
        subsubnumingroup=new Int_t*[nsubsearch+1];
        subsubpglist=new Int_t**[nsubsearch+1];
        subngroup=new Int_t[nsubsearch+1];
        numcores=new Int_t[nsubsearch+1];
        ns=0;
        //here loop over all sublevel groups that need to be searched for substructure
        for (Int_t i=1;i<=oldnsubsearch;i++) {
            subPart=new Particle[subnumingroup[i]];
            for (Int_t j=0;j<subnumingroup[i];j++) subPart[j]=Partsubset[subpglist[i][j]];
            //now if low statistics, then possible that very central regions of subhalo will be higher due to cell size used and Nv search
            //so first determine centre of subregion
            Double_t cmx=0.,cmy=0.,cmz=0.,cmvelx=0.,cmvely=0.,cmvelz=0.;
            Double_t mtotregion=0.0;
            Int_t j;
            if (opt.icmrefadjust) {
                if(opt.iverbose) cout<<"moving to cm frame"<<endl;
#ifdef USEOPENMP
            if (subnumingroup[i]>ompsearchnum) {
#pragma omp parallel default(shared) 
{
#pragma omp for private(j) reduction(+:mtotregion,cmx,cmy,cmz,cmvelx,cmvely,cmvelz)
            for (j=0;j<subnumingroup[i];j++) {
                cmx+=subPart[j].X()*subPart[j].GetMass();
                cmy+=subPart[j].Y()*subPart[j].GetMass();
                cmz+=subPart[j].Z()*subPart[j].GetMass();
                cmvelx+=subPart[j].Vx()*subPart[j].GetMass();
                cmvely+=subPart[j].Vy()*subPart[j].GetMass();
                cmvelz+=subPart[j].Vz()*subPart[j].GetMass();
                mtotregion+=subPart[j].GetMass();
            }
}
            }
            else {
#endif
            for (j=0;j<subnumingroup[i];j++) {
                cmx+=subPart[j].X()*subPart[j].GetMass();
                cmy+=subPart[j].Y()*subPart[j].GetMass();
                cmz+=subPart[j].Z()*subPart[j].GetMass();
                cmvelx+=subPart[j].Vx()*subPart[j].GetMass();
                cmvely+=subPart[j].Vy()*subPart[j].GetMass();
                cmvelz+=subPart[j].Vz()*subPart[j].GetMass();
                mtotregion+=subPart[j].GetMass();
            }
#ifdef USEOPENMP
}
#endif
            cm[0]=cmx;cm[1]=cmy;cm[2]=cmz;
            cmvel[0]=cmvelx;cmvel[1]=cmvely;cmvel[2]=cmvelz;
            for (int k=0;k<3;k++) {cm[k]/=mtotregion;cmvel[k]/=mtotregion;}
#ifdef USEOPENMP
            if (subnumingroup[i]>ompsearchnum) {
#pragma omp parallel default(shared) 
{
#pragma omp for private(j) 
            for (j=0;j<subnumingroup[i];j++) 
                for (int k=0;k<3;k++) {
                    subPart[j].SetPosition(k,subPart[j].GetPosition(k)-cm[k]);subPart[j].SetVelocity(k,subPart[j].GetVelocity(k)-cmvel[k]);
                }
}
            }
            else {
#endif
            for (j=0;j<subnumingroup[i];j++) 
                for (int k=0;k<3;k++) {
                    subPart[j].SetPosition(k,subPart[j].GetPosition(k)-cm[k]);subPart[j].SetVelocity(k,subPart[j].GetVelocity(k)-cmvel[k]);
                }
#ifdef USEOPENMP
}
#endif
            }
            opt.Ncell=opt.Ncellfac*subnumingroup[i];
            //if ncell is such that uncertainty would be greater than 0.5% based on Poisson noise, increase ncell till above unless cell would contain >25% 
            while (opt.Ncell<MINCELLSIZE && subnumingroup[i]/4.0>opt.Ncell) opt.Ncell*=2;
            tree=InitializeTreeGrid(opt,subnumingroup[i],subPart);
            ngrid=tree->GetNumLeafNodes();
            if (opt.iverbose) cout<<ThisTask<<" Substructure "<<i<< " at sublevel "<<sublevel<<" with "<<subnumingroup[i]<<" particles split into are "<<ngrid<<" grid cells, with each node containing ~"<<subnumingroup[i]/ngrid<<" particles"<<endl;
            grid=new GridCell[ngrid];
            FillTreeGrid(opt, subnumingroup[i], ngrid, tree, subPart, grid);
            gvel=GetCellVel(opt,subnumingroup[i],subPart,ngrid,grid);
            gveldisp=GetCellVelDisp(opt,subnumingroup[i],subPart,ngrid,grid,gvel);
            opt.HaloSigmaV=0;for (int j=0;j<ngrid;j++) opt.HaloSigmaV+=pow(gveldisp[j].Det(),1./3.);opt.HaloSigmaV/=(double)ngrid;
            //store the maximum halo velocity scale 
            if (opt.HaloSigmaV>opt.HaloVelDispScale) opt.HaloVelDispScale=opt.HaloSigmaV;
            //now if object is large enough for phase-space decomposition and search, compare local field to bg field
            if (subnumingroup[i]>=MINSUBSIZE) {
#ifdef HALOONLYDEN
                GetVelocityDensity(opt,subnumingroup[i],subPart);
#endif
                GetDenVRatio(opt,subnumingroup[i],subPart,ngrid,grid,gvel,gveldisp);
                int blah=GetOutliersValues(opt,subnumingroup[i],subPart,sublevel);
                opt.idenvflag++;//largest field halo used to deteremine statistics of ratio
            }
            subpfof=SearchSubset(opt,subnumingroup[i],subnumingroup[i],subPart,subngroup[i],sublevel,&numcores[i]);
            //now if subngroup>0 change the pfof ids of these particles in question and see if there are any substrucures that can be searched again.
            //the group ids must be stored along with the number of groups in this substructure that will be searched at next level.
            //now check if self bound and if not, id doesn't change from original subhalo,ie: subpfof[j]=0
            if (subngroup[i]) {
                ng=subngroup[i];
                subsubnumingroup[i]=BuildNumInGroup(subnumingroup[i], subngroup[i], subpfof);
                subsubpglist[i]=BuildPGList(subnumingroup[i], subngroup[i], subsubnumingroup[i], subpfof);
                if (opt.uinfo.unbindflag&&subngroup[i]>0) {
                    CheckUnboundGroups(opt,subnumingroup[i],subPart,subngroup[i],subpfof,subsubnumingroup[i],subsubpglist[i]);
                    if (subngroup[i]!=ng) {
                        for (int j=1;j<=ng;j++) delete[] subsubpglist[i][j];
                        delete[] subsubnumingroup[i];
                        delete[] subsubpglist[i];
                        if (subngroup[i]>0) {
                            subsubnumingroup[i]=BuildNumInGroup(subnumingroup[i], subngroup[i], subpfof);
                            subsubpglist[i]=BuildPGList(subnumingroup[i], subngroup[i], subsubnumingroup[i], subpfof);
                        }
                    }
                }
                for (j=0;j<subnumingroup[i];j++) if (subpfof[j]>0) pfof[subpglist[i][j]]=ngroup+ngroupidoffset+subpfof[j];
                ngroupidoffset+=subngroup[i];
                //now alter subsubpglist so that index pointed is global subset index as global subset is used to get the particles to be searched for subsubstructure
                for (j=1;j<=subngroup[i];j++) for (Int_t k=0;k<subsubnumingroup[i][j];k++) subsubpglist[i][j][k]=subpglist[i][subsubpglist[i][j][k]];
            }
            delete[] subpfof;
            delete[] subPart;
            //increase tot num of objects at sublevel
            ns+=subngroup[i];
        }
        //if objects have been found adjust the StrucLevelData
        //this stores the address of the parent particle and pfof along with child substructure particle and pfof
        if (ns>0) {
            pcsld->nextlevel=new StrucLevelData();
            //pcsld->nextlevel->Initialize(ns);
            pcsld->nextlevel->Phead=new Particle*[ns+1];
            pcsld->nextlevel->gidhead=new Int_t*[ns+1];
            pcsld->nextlevel->Pparenthead=new Particle*[ns+1];
            pcsld->nextlevel->gidparenthead=new Int_t*[ns+1];
            pcsld->nextlevel->giduberparenthead=new Int_t*[ns+1];
            pcsld->nextlevel->stypeinlevel=new Int_t[ns+1];
            pcsld->nextlevel->stype=HALOSTYPE+10*sublevel;
            pcsld->nextlevel->nsinlevel=ns;
            /// \todo need to adjust addressing here as i is not in the same order as
            /// the index of pcsld arrays
            Int_t nscount=1;
            for (Int_t i=1;i<=oldnsubsearch;i++) {
                Int_t ii=0,iindex;
                //here adjust head particle of parent structure if necessary
                while (pfof[subpglist[i][ii]]>ngroup+ngroupidoffset-ns) ii++;
                //store 
                iindex=pfof[subpglist[i][ii]]-ngroupidoffsetold;
                pcsld->gidhead[iindex]=&pfof[subpglist[i][ii]];
                pcsld->Phead[iindex]=&Partsubset[subpglist[i][ii]];
                //only for field haloes does the gidparenthead and giduberparenthead need to be adjusted
                if(sublevel==1) {
                    pcsld->gidparenthead[iindex]=&pfof[subpglist[i][ii]];
                    pcsld->giduberparenthead[iindex]=&pfof[subpglist[i][ii]];
                }
                for (Int_t j=1;j<=subngroup[i];j++) {
                    //need to restructure pointers so that they point to what the parent halo
                    //points to and point to the head of the structure
                    //this is after viable structures have been identfied
                    pcsld->nextlevel->Pparenthead[nscount]=&Partsubset[subpglist[i][ii]];
                    pcsld->nextlevel->gidparenthead[nscount]=&pfof[subpglist[i][ii]];
                    pcsld->nextlevel->Phead[nscount]=&Partsubset[subsubpglist[i][j][0]];
                    pcsld->nextlevel->gidhead[nscount]=&pfof[subsubpglist[i][j][0]];
                    pcsld->nextlevel->giduberparenthead[nscount]=pcsld->giduberparenthead[i];
                    //if multiple core region then set the appropriate structure level
                    if (j>=(subngroup[i]-numcores[i])+1) pcsld->nextlevel->stypeinlevel[nscount]=HALOSTYPE+10*(sublevel-1)+HALOCORESTYPE;
                    else pcsld->nextlevel->stypeinlevel[nscount]=HALOSTYPE+10*sublevel;
                    nscount++;
                }
            }
            ngroupidoffsetold+=pcsld->nsinlevel;
            pcsld=pcsld->nextlevel;
        }
        if (opt.iverbose) cout<<ThisTask<<"Finished searching substructures to sublevel "<<sublevel<<endl;
        sublevel++;
        for (Int_t i=1;i<=oldnsubsearch;i++) delete[] subpglist[i];
        delete[] subpglist;
        delete[] subnumingroup;
        nsubsearch=0;
        //after looping over all level sublevel substructures adjust nsubsearch, set subpglist subnumingroup, so that can move to next level.
        for (Int_t i=1;i<=oldnsubsearch;i++) 
            for (Int_t j=1;j<=subngroup[i];j++) if (subsubnumingroup[i][j]>MINSUBSIZE) 
                nsubsearch++;
        if (nsubsearch>0) {
            subnumingroup=new Int_t[nsubsearch+1];
            subpglist=new Int_t*[nsubsearch+1];
            nsubsearch=1;
            for (Int_t i=1;i<=oldnsubsearch;i++) {
                for (Int_t j=1;j<=subngroup[i];j++) 
                    if (subsubnumingroup[i][j]>MINSUBSIZE) {
                        subnumingroup[nsubsearch]=subsubnumingroup[i][j];
                        subpglist[nsubsearch]=new Int_t[subnumingroup[nsubsearch]];
                        for (Int_t k=0;k<subnumingroup[nsubsearch];k++) subpglist[nsubsearch][k]=subsubpglist[i][j][k];
                        nsubsearch++;
                    }
            }
            nsubsearch--;
        }
        else iflag=false;
        for (Int_t i=1;i<=oldnsubsearch;i++) {
            if (subngroup[i]>0) {
                for (Int_t j=1;j<=subngroup[i];j++) delete[] subsubpglist[i][j];
                delete[] subsubnumingroup[i];
                delete[] subsubpglist[i];
            }
        }
        delete[] subsubnumingroup;
        delete[] subsubpglist;
        delete[] subngroup;
        delete[] numcores;
        if (opt.iverbose) cout<<ThisTask<<"Finished storing next level of substructures to be searched for subsubstructure"<<endl;
    }

    ngroup+=ngroupidoffset;
    cout<<ThisTask<<"Done searching substructure to "<<sublevel-1<<" sublevels "<<endl;
    }
    else delete[] numingroup;
    //if not an idividual halo and want bound haloes (and not searching for baryons afterwards) 
    //unbind halo population 
    if (!opt.iSingleHalo&&opt.iBoundHalos&&!opt.iBaryonSearch) {
        //begin by storing information of the current hierarchy
        Int_t nhaloidoffset=0,nhierarchy,gidval;
        Int_t *nsub,*parentgid,*uparentgid,*stype;
        StrucLevelData *ppsldata,**papsldata;

        ng=nhalos;
        numingroup=BuildNumInGroup(nsubset, ngroup, pfof);
        pglist=BuildPGList(nsubset, ngroup, numingroup, pfof);

        //store the parent and uber parent info
        nsub=new Int_t[ngroup+1];
        parentgid=new Int_t[ngroup+1];
        uparentgid=new Int_t[ngroup+1];
        stype=new Int_t[ngroup+1];
        GetHierarchy(opt,ngroup,nsub,parentgid,uparentgid,stype);
        //store pointer to the various substructure levels
        ppsldata=psldata;
        nhierarchy=0;
        //determine the number of levels in the hierarchy
        while (ppsldata->nextlevel!=NULL){nhierarchy++;ppsldata=ppsldata->nextlevel;}
        ppsldata=psldata;
        papsldata=new StrucLevelData*[nhierarchy];
        nhierarchy=0;
        while (ppsldata!=NULL) {papsldata[nhierarchy++]=ppsldata;ppsldata=ppsldata->nextlevel;}

        if(CheckUnboundGroups(opt,nsubset,Partsubset,nhalos,pfof,numingroup,pglist,0)) {
            //if haloes adjusted then need to update the StrucLevelData
            //first update just halos (here ng=old nhalos)
            //by setting NULL values in structure level and moving all the unbound halos the end of array
            for (Int_t i=1;i<=ng;i++) {
                if (numingroup[i]==0) {
                    psldata->Phead[i]=NULL;
                    psldata->gidhead[i]=NULL;
                }
                else {
                    psldata->Phead[i]=&Partsubset[pglist[i][0]];
                    psldata->gidhead[i]=&pfof[pglist[i][0]];
                    //set the parent pointers to appropriate addresss such that the parent and uber parent are the same as the groups head
                    psldata->gidparenthead[i]=psldata->gidhead[i];
                    psldata->giduberparenthead[i]=psldata->gidhead[i];
                }
            }
            for (Int_t i=1,j=0;i<=nhalos;i++) {
                if (psldata->Phead[i]==NULL) {
                    j=i+1;while(psldata->Phead[j]==NULL) j++;
                    //copy the first non-NULL pointer to current NULL pointers,
                    //ie: for a non-existing structure which has been unbound, remove it from the structure list
                    //by copying the pointers of the next still viable structure to that address and setting 
                    //the pointers at the new position, j, to NULL
                    psldata->Phead[i]=psldata->Phead[j];
                    psldata->gidhead[i]=psldata->gidhead[j];
                    psldata->gidparenthead[i]=psldata->gidparenthead[j];
                    psldata->giduberparenthead[i]=psldata->giduberparenthead[j];
                    psldata->stypeinlevel[i]=psldata->stypeinlevel[j];
                    psldata->Phead[j]=NULL;
                    psldata->gidhead[j]=NULL;
                    psldata->gidparenthead[j]=NULL;
                    psldata->giduberparenthead[j]=NULL;
                    psldata->stypeinlevel[j]=BGTYPE;
                }
            }
            psldata->nsinlevel=nhalos;
            //then adjust sublevels so that they point to the appropriate parent and uberparent values
            for (Int_t i=nhierarchy-1;i>0;i--){
                for (int j=1;j<=papsldata[i]->nsinlevel;j++) {
                    gidval=(*papsldata[i]->gidhead[j]);
                    if (parentgid[gidval]!=-1) {
                        if (numingroup[parentgid[gidval]]>0) papsldata[i]->gidparenthead[j]=&pfof[pglist[parentgid[gidval]][0]];
                        else papsldata[i]->gidparenthead[j]=NULL;
                    }
                    if (uparentgid[gidval]!=-1) {
                        if (numingroup[uparentgid[gidval]]>0) papsldata[i]->giduberparenthead[j]=&pfof[pglist[uparentgid[gidval]][0]];
                        else papsldata[i]->giduberparenthead[j]=NULL;
                    }
                }
            }
            //adjust halo ids
            ReorderGroupIDs(ng,nhalos, numingroup, pfof,pglist);
            nhaloidoffset=ng-nhalos;
            for (Int_t i=0;i<nsubset;i++) if (pfof[i]>ng) pfof[i]-=nhaloidoffset;
        }
        for (Int_t i=1;i<=ngroup;i++) delete[] pglist[i];
        delete[] pglist;
        delete[] numingroup;
        delete[] nsub;
        delete[] parentgid;
        delete[] uparentgid;
        delete[] papsldata;
        ngroup-=nhaloidoffset;
    }
    }
    //update the number of local groups found
#ifdef USEMPI
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Allgather(&ngroup, 1, MPI_Int_t, mpi_ngroups, 1, MPI_Int_t, MPI_COMM_WORLD);
    cout<<ThisTask<<" has found a total of "<<ngroup<<endl;
#endif
}

/*! 
    Check significance of group using significance parameter.  A group is considered significant if (ave/expected ave) (and possibly (max-min)/varexpected] is significant relative to Poisson noise. 
    If a group is not start removing particle with lowest ell value.
*/
int CheckSignificance(Options &opt, const Int_t nsubset, Particle *Partsubset, Int_t &numgroups, Int_t *numingroup, Int_t *pfof, Int_t **pglist)
{
    Int_t i;
    Double_t ellaveexp,ellvarexp, ellvallim;
    Double_t *aveell,*varell,*maxell,*minell, *ptypefrac, *betaave;
    Int_t iflag=0,ng=numgroups;
    aveell=new Double_t[numgroups+1];
    maxell=new Double_t[numgroups+1];
    minell=new Double_t[numgroups+1];
    //varell=new Double_t[numgroups+1];
    betaave=new Double_t[numgroups+1];

    if (opt.iiterflag) ellvallim=opt.ellthreshold*opt.ellfac;
    else ellvallim=opt.ellthreshold;
    ellaveexp=sqrt(2.0/M_PI)*exp(-ellvallim*ellvallim)*exp(0.5*ellvallim*ellvallim)/(1.0-gsl_sf_erf(ellvallim/sqrt(2.0)));

    //first adjust system and store pList for each group so one can access pfof appropriately
    if (opt.iverbose) {
        cout<<"Checking that groups have a significance level of "<<opt.siglevel<<" and contain more than "<<opt.MinSize<<" members"<<endl;
        cout<<"First calculate group properties"<<endl;
    }
    for (i=0;i<=numgroups;i++) {aveell[i]=0.;maxell[i]=-MAXVALUE;minell[i]=MAXVALUE;//numingroup[i]=0;
    }
    for (i=0;i<nsubset;i++)
    {
        Int_t gid=pfof[Partsubset[i].GetID()];
        Double_t ellvalue=Partsubset[i].GetPotential();
        aveell[gid]+=ellvalue;
        if(maxell[gid]<ellvalue)maxell[gid]=ellvalue;
        if(minell[gid]>ellvalue)minell[gid]=ellvalue;
    }
    for (i=1;i<=numgroups;i++) {
        aveell[i]/=(Double_t)numingroup[i];
        betaave[i]=(aveell[i]/ellaveexp-1.0)*sqrt((Double_t)numingroup[i]); 
        //flag indicating that group ids need to be adjusted 
        if(betaave[i]<opt.siglevel) iflag=1;
    }
    if (opt.iverbose) cout<<"Done"<<endl;
    if (iflag){
        if (opt.iverbose) cout<<"Remove groups below significance level"<<endl;
#ifdef USEOPENMP
#pragma omp parallel default(shared) \
private(i)
{
#pragma omp for
#endif
        for (i=1;i<=numgroups;i++) {
            if(betaave[i]<opt.siglevel) {
                do {
                    if ((numingroup[i])<opt.MinSize) {
                        for (Int_t j=0;j<numingroup[i];j++) pfof[Partsubset[pglist[i][j]].GetID()]=0;
                        numingroup[i]=-1;
                        break;
                    }
                    Int_t iminell;
                    Double_t vminell=minell[i];
                    aveell[i]=(aveell[i]*(Double_t)numingroup[i]-minell[i])/(Double_t)(numingroup[i]-1.0);
                    minell[i]=maxell[i];
                    for (Int_t j=0;j<numingroup[i];j++) 
                        if(Partsubset[pglist[i][j]].GetPotential()>vminell&&Partsubset[pglist[i][j]].GetPotential()<minell[i])minell[i]=Partsubset[pglist[i][j]].GetPotential();
                        else if (Partsubset[pglist[i][j]].GetPotential()==vminell) iminell=j;
                    pfof[Partsubset[pglist[i][iminell]].GetID()]=0;
                    if (iminell!=numingroup[i]-1)pglist[i][iminell]=pglist[i][--numingroup[i]];
                    betaave[i]=(aveell[i]/ellaveexp-1.0)*sqrt((Double_t)numingroup[i]);
                } while(betaave[i]<opt.siglevel);
            }
            else if ((numingroup[i])<opt.MinSize) {
                for (Int_t j=0;j<numingroup[i];j++) pfof[Partsubset[pglist[i][j]].GetID()]=0;
                numingroup[i]=-1;
            }
        }
#ifdef USEOPENMP
}
#endif
        if (opt.iverbose) cout<<"Done"<<endl;
        for (i=1;i<=numgroups;i++) if (numingroup[i]==-1) ng--;
        if (ng) ReorderGroupIDs(numgroups, ng, numingroup, pfof, pglist, Partsubset);
        else if (opt.iverbose) printf("No groups of significance found\n");
    }

    //free memory
    delete[] aveell;
    delete[] maxell;
    delete[] minell;
    delete[] betaave;

    numgroups=ng;
    return iflag;
}

//@}

/// \name Routines searches baryonic or other components separately based on initial dark matter (or other) search
//@{
/*! 
 * Searches star and gas particles separately to see if they are associated with any dark matter particles belonging to a substructure
 * 
 * \todo assumes baryonic particles that could be associated with haloes located in MPI's thread are also on that thread.
 * Must alter so as to allow searches accross MPI domains but this is tricky considering that domains are NOT simple cells
 * However, one can just determine the extent of minimal distance of all baryonic particles in physical space and then
 * search all mpi domains based on that distance. Or determine all structures that lie on the boundary of the mpi domain, 
 * export those to other mpi domains for a search.
 * In MPI maybe more appropriate to MPI to search gas particles that might be in search radius of dm particle lying within a group
 * 
 * \todo at the moment a gas/star particle is associated with a unique structure. If that happens to be a substructure, and this gas/star particle is not bound
 * to the substructure, it is NOT tested to see if it should be associated with the parent structure. This will have to be changed at some point. Which may mean 
 * searching for baryons at each stage of a structure search. That is one needs to search for baryons after field objects have been found, then in SubSubSearch loop.
 * This will require a rewrite of the code. OR! the criterion checked actually has information on the local potential and kinetic reference frame so that we can 
 * check on the fly whether a particle is approximately bound. This simplest choice would be to store the potential of the DM particles, assume the gas particle's potential is that of
 * its nearest dm particle and store the kinetic centre of a group to see if the particle is bound.
*/
Int_t* SearchBaryons(Options &opt, Int_t &nbaryons, Particle *&Pbaryons, const Int_t ndark, Particle *&Part, Int_t *&pfofdark, Int_t &ngroupdark, Int_t &nhalos, int ihaloflag, int iinclusive, PropData *pdata)
{
    KDTree *tree;
    Double_t *period;
    Int_t *pfofbaryons, *pfofall, i,pindex,npartingroups,ng,nghalos,nhalosold=nhalos;
    Int_t *ids, *storeval,*storeval2;
    Double_t D2,dval,rval;
    Coordinate x1;
    Particle p1;
    int icheck;
    FOFcompfunc fofcmp;
    Double_t param[20];
    int nsearch=opt.Nvel;
    Int_t **nnID,*numingroup;
    Double_t **dist2, *localdist;
    int nthreads=1,maxnthreads,tid;
    int minsize;
    Int_t nparts=ndark+nbaryons;
    Int_t nhierarchy=1,gidval;
    StrucLevelData *ppsldata,**papsldata;
#ifndef USEMPI
    int ThisTask=0,NProcs=1;
#endif

    cout<<ThisTask<<" search baryons "<<nparts<<" "<<ndark<<endl;
    if (opt.partsearchtype==PSTALL) {
        cout<<" of only substructures as baryons have already been grouped in FOF search "<<endl;
        //store original pfof value
        pfofall=pfofdark;//new Int_t[nparts];
        pfofbaryons=&pfofall[ndark];
	    storeval=new Int_t[nparts];
    	storeval2=new Int_t[nparts];
        for (i=0;i<nparts;i++) {
            if (Part[i].GetType()==DARKTYPE) Part[i].SetType(-1);
	        storeval2[i]=Part[i].GetPID();
		    Part[i].SetPID(pfofdark[i]);
        }
        qsort(Part,nparts,sizeof(Particle),TypeCompare);
        Pbaryons=&Part[ndark];
        for (i=0;i<nparts;i++) {
	         //store id order after type sort
	        storeval[i]=Part[i].GetID();
            Part[i].SetID(i);
	        pfofdark[i]=Part[i].GetPID();
        }
    }
#ifdef USEOPENMP
#pragma omp parallel
    {
    if (omp_get_thread_num()==0) maxnthreads=omp_get_num_threads();
    if (omp_get_thread_num()==0) nthreads=omp_get_num_threads();
    }
#endif
    //if serial can allocate pfofall at this point as the number of particles in local memory will not change
#ifndef USEMPI
    if (opt.partsearchtype!=PSTALL) {
        pfofall=new Int_t[nbaryons+ndark];
        for (i=0;i<ndark;i++) pfofall[i]=pfofdark[i];
        pfofbaryons=&pfofall[ndark];
        for (i=0;i<nbaryons;i++) pfofbaryons[i]=0;
    }
#else
    //if using mpi but all particle FOF search, then everything is localized
    if (opt.partsearchtype!=PSTALL) {
        pfofbaryons=new Int_t[nbaryons];
        for (i=0;i<nbaryons;i++) pfofbaryons[i]=0;
        localdist=new Double_t[nbaryons];
        for (i=0;i<nbaryons;i++) localdist[i]=MAXVALUE;
    }
#endif

#ifndef USEMPI
    if (ngroupdark==0) return pfofall;
#endif
    numingroup=BuildNumInGroup(ndark, ngroupdark, pfofdark);

    //determine total number of particles in groups and set search appropriately
    npartingroups=0;
    for (i=1;i<=ngroupdark;i++) npartingroups+=numingroup[i];
    if (npartingroups<=2*opt.MinSize) nsearch=npartingroups;
    else nsearch=2*opt.MinSize;
    if (opt.p>0) {
        period=new Double_t[3];
        for (int j=0;j<3;j++) period[j]=opt.p;
    }

    //sort particles so that first group is first, then all others in groups
    if (opt.iverbose) cout<<"sort particles so that tree only uses particles in groups "<<npartingroups<<endl;
/*    if (opt.partsearchtype!=PSTALL) {
        for (i=0;i<ndark;i++) Part[i].SetPotential(2*(pfofdark[i]==0)+(pfofdark[i]>1));
    }
    else {
        //only search association to substructures
        for (i=0;i<ndark;i++) Part[i].SetPotential(2*(pfofdark[i]<=nhalos)+(pfofdark[i]>nhalos));        
    }*/
    for (i=0;i<ndark;i++) Part[i].SetPotential(2*(pfofdark[i]==0)+(pfofdark[i]>1));
    qsort(Part, ndark, sizeof(Particle), PotCompare);
    ids=new Int_t[ndark+1];
    for (i=0;i<ndark;i++) ids[i]=Part[i].GetID();

    if (npartingroups>0) {
    //set parameters, first to some fraction of halo linking length
    param[1]=(opt.ellxscale*opt.ellxscale)*(opt.ellphys*opt.ellphys)*(opt.ellhalophysfac*opt.ellhalophysfac);
    param[6]=param[1];
    //also check to see if velocity scale still zero find dispersion of largest halo
    //otherwise search uses the largest average "local" velocity dispersion of halo identified in the mpi domain. 
    if (opt.HaloVelDispScale==0) {
        Double_t mtotregion,vx,vy,vz;
        Coordinate vmean;
        mtotregion=vx=vy=vz=0;
        for (i=0;i<numingroup[1];i++) {
            vx+=Part[i].GetVelocity(0)*Part[i].GetMass();
            vy+=Part[i].GetVelocity(1)*Part[i].GetMass();
            vz+=Part[i].GetVelocity(2)*Part[i].GetMass();
            mtotregion+=Part[i].GetMass();
        }
        vmean[0]=vx/mtotregion;vmean[1]=vy/mtotregion;vmean[2]=vz/mtotregion;
        for (i=0;i<numingroup[1];i++) {
            for (int j=0;j<3;j++) opt.HaloVelDispScale+=pow(Part[i].GetVelocity(j)-vmean[j],2.0)*Part[i].GetMass();
        }
        opt.HaloVelDispScale/=mtotregion;
        param[2]=opt.HaloVelDispScale;
    }
    else param[2]=opt.HaloVelDispScale*16.0;//here use factor of 4 in local dispersion //could remove entirely and just use global dispersion but this will over compensate.
    param[7]=param[2];

    //Set fof type
    fofcmp=&FOF6d;
    if (opt.iverbose) {
    cout<<"Baryon search "<<nbaryons<<endl;
    cout<<"FOF6D uses ellphys and ellvel.\n";
    cout<<"Parameters used are : ellphys="<<sqrt(param[6])<<" Lunits, ellvel="<<sqrt(param[7])<<" Vunits.\n";
    cout<<"Building tree to search dm containing "<<npartingroups<<endl;
    }
    tree=new KDTree(Part,npartingroups,nsearch/2,tree->TPHYS,tree->KEPAN,100,0,0,0,period);
    //allocate memory for search
    nnID=new Int_t*[nthreads];
    dist2=new Double_t*[nthreads];
    for (i=0;i<nthreads;i++) {
        nnID[i]=new Int_t[nsearch];
        dist2[i]=new Double_t[nsearch];
    }
    //find the closest dm particle that belongs to the largest dm group and associate the baryon with that group (including phase-space window)
    if (opt.iverbose) cout<<"Searching ..."<<endl;
#ifdef USEOPENMP
#pragma omp parallel default(shared) \
private(i,tid,p1,pindex,x1,D2,dval,rval,icheck)
{
#pragma omp for
#endif
    for (i=0;i<nbaryons;i++)
    {
#ifdef USEOPENMP
        tid=omp_get_thread_num();
#else
        tid=0;
#endif
        if (opt.partsearchtype==PSTALL && pfofbaryons[i]==0) continue;
        p1=Pbaryons[i];
        x1=Coordinate(p1.GetPosition());
        rval=dval=MAXVALUE;
        tree->FindNearestPos(x1, nnID[tid], dist2[tid],nsearch);
        if (dist2[tid][0]<param[6]) {
        for (int j=0;j<nsearch;j++) {
            D2=0;
            pindex=ids[Part[nnID[tid][j]].GetID()];
            if (opt.partsearchtype==PSTALL) icheck=1;
            else icheck=(numingroup[pfofbaryons[i]]<numingroup[pfofdark[pindex]]);
            if (icheck) {
                if (fofcmp(p1,Part[nnID[tid][j]],param)) {
                    for (int k=0;k<3;k++) {
                        D2+=(p1.GetPosition(k)-Part[nnID[tid][j]].GetPosition(k))*(p1.GetPosition(k)-Part[nnID[tid][j]].GetPosition(k))/param[6]+(p1.GetVelocity(k)-Part[nnID[tid][j]].GetVelocity(k))*(p1.GetVelocity(k)-Part[nnID[tid][j]].GetVelocity(k))/param[7];
                    }
                    //if gas thermal properties stored then also add self-energy to distance measure
#ifdef GASON
                    D2+=p1.GetU()/param[7];
#endif
                    //check to see if phase-space distance is small 
                    if (dval>D2) {
                        dval=D2;pfofbaryons[i]=pfofdark[pindex];
                        rval=dist2[tid][j];
#ifdef USEMPI
                        if (opt.partsearchtype!=PSTALL) localdist[i]=dval;
#endif
                    }
                }
            }
        }
        }
    }
#ifdef USEOPENMP
}
#endif
    }
#ifdef USEMPI
    if (opt.partsearchtype!=PSTALL) {
    if (opt.iverbose) cout<<ThisTask<<" finished local search"<<endl;
    MPI_Barrier(MPI_COMM_WORLD);
    //determine all tagged dark matter particles that have search areas that overlap another mpi domain
    MPIGetExportNum(npartingroups, Part, sqrt(param[1]));
    //to store local mpi task 
    mpi_foftask=new Int_t[nbaryons];
    //then determine export particles, declare arrays used to export data
    PartDataIn = new Particle[NExport+1];
    PartDataGet = new Particle[NImport+1];
    FoFDataIn = new fofdata_in[NExport+1];
    FoFDataGet = new fofdata_in[NImport+1];
    //exchange particles
    MPISetTaskID(nbaryons);
    MPIBuildParticleExportBaryonSearchList(npartingroups, Part, pfofdark, ids, numingroup, sqrt(param[1]));

    //now dark matter particles associated with a group existing on another mpi domain are local and can be searched. 
    NExport=MPISearchBaryons(nbaryons, Pbaryons, pfofbaryons, numingroup, localdist, nsearch, param, period, nnID, dist2);

    //reset order
    delete tree;
    for (i=0;i<ndark;i++) Part[i].SetID(ids[i]);
    qsort(Part, ndark, sizeof(Particle), IDCompare);
    delete[] ids;

    //reorder local particle array and delete memory associated with Head arrays, only need to keep Particles, pfof and some id and idexing information
    delete[] FoFDataIn;
    delete[] FoFDataGet;
    delete[] PartDataIn;
    delete[] PartDataGet;

    Int_t newnbaryons=MPIBaryonGroupExchange(nbaryons,Pbaryons,pfofbaryons);
    //once baryons are correctly associated to the appropriate mpi domain and are local (either in Pbaryons or in the \ref fofid_in structure, specifically FOFGroupData arrays) must then copy info correctly.
#ifdef MPIREDUCEMEM
    if (Nmemlocalbaryon<newnbaryons) 
#endif
    {
    //note that if mpireduce is not set then all info is copied into the FOFGroupData structure and must deallocated and reallocate Pbaryon array
    delete[] Pbaryons;
    Pbaryons=new Particle[newnbaryons];
    delete[] pfofbaryons;
    pfofbaryons=new Int_t[newnbaryons];
    }
    //then compile groups and if inclusive halo masses not calculated, reorder group ids
    MPIBaryonCompileGroups(newnbaryons,Pbaryons,pfofbaryons,opt.MinSize,(opt.iInclusiveHalo==0));
    delete[] mpi_foftask;
    if (opt.iverbose) cout<<ThisTask<<" finished search across domains"<<endl;
    //now allocate pfofall and store info
    pfofall=new Int_t[newnbaryons+ndark];
    for (i=0;i<ndark;i++) pfofall[i]=pfofdark[i];
    for (i=0;i<newnbaryons;i++) pfofall[i+ndark]=pfofbaryons[i];
    delete[] pfofbaryons;

    //update nbaryons
    nbaryons=newnbaryons;
    Nlocalbaryon[0]=newnbaryons;


    //and place all particles into a contiguous memory block
    nparts=ndark+nbaryons;
    mpi_Part2=new Particle[nparts];
    for (i=0;i<ndark;i++)mpi_Part2[i]=Part[i];
    for (i=0;i<nbaryons;i++)mpi_Part2[i+ndark]=Pbaryons[i];
    if (mpi_Part1!=NULL) delete[] mpi_Part1;
    else delete[] Part;
    delete[] Pbaryons;
    Part=mpi_Part2;
    Pbaryons=&mpi_Part2[ndark];
    for (i=0;i<nbaryons;i++) Pbaryons[i].SetID(i+ndark);
    Nlocal=nparts;
    }
    else {
        //reset order
        if (npartingroups>0) delete tree;
      	for (i=0;i<ndark;i++) Part[i].SetID(ids[i]);
        qsort(Part, ndark, sizeof(Particle), IDCompare);
        delete[] ids;
        for (i=0;i<nparts;i++) {Part[i].SetPID(pfofall[Part[i].GetID()]);Part[i].SetID(storeval[i]);}
        qsort(Part, nparts, sizeof(Particle), IDCompare);
	    for (i=0;i<nparts;i++) {
            pfofall[i]=Part[i].GetPID();Part[i].SetPID(storeval2[i]);
            if (Part[i].GetType()==-1)Part[i].SetType(DARKTYPE);
        }
    	delete[] storeval;
    	delete[] storeval2;
    }
#else
    delete tree;
    for (i=0;i<ndark;i++) Part[i].SetID(ids[i]);
    qsort(Part, ndark, sizeof(Particle), IDCompare);
    delete[] ids;
    for (i=0;i<nbaryons;i++) Pbaryons[i].SetID(i+ndark);
#endif
    //and free up memory
    delete[] numingroup;
    if (npartingroups>0) {
        for (int j=0;j<nthreads;j++) {delete[] nnID[j];delete[] dist2[j];}
        delete[] nnID;
        delete[] dist2;
    }

    cout<<"Done"<<endl;

    //if unbinding go back and redo full unbinding after including baryons
    if (opt.uinfo.unbindflag&&ngroupdark>0) {
        if (opt.iverbose) cout<<ThisTask<<" starting unbind of dm+baryons"<<endl;
        //build new arrays based on pfofall. 
        Int_t *ningall,**pglistall;
        ningall=BuildNumInGroup(nparts, ngroupdark, pfofall);
        pglistall=BuildPGList(nparts, ngroupdark, ningall, pfofall);
        //first repoint everything in the structure pointer to pfofall. See GetHierarchy for some guidance on how to parse the 
        //the structure data
        nhierarchy=1;
        ppsldata=psldata;
        //determine the number of levels in the hierarchy
        while (ppsldata->nextlevel!=NULL){nhierarchy++;ppsldata=ppsldata->nextlevel;}
        ppsldata=psldata;
        papsldata=new StrucLevelData*[nhierarchy];
        nhierarchy=0;
        while (ppsldata!=NULL) {papsldata[nhierarchy++]=ppsldata;ppsldata=ppsldata->nextlevel;}
        //now parse hierarchy and repoint stuff to the pfofall pointer instead of pfof
        if (opt.partsearchtype!=PSTALL) {
        for (i=nhierarchy-1;i>=0;i--){
            for (int j=1;j<=papsldata[i]->nsinlevel;j++) {
                gidval=(*papsldata[i]->gidhead[j]);
                papsldata[i]->gidhead[j]=&pfofall[pglistall[gidval][0]];
                if (papsldata[i]->gidparenthead[j]!=NULL) {
                    gidval=(*papsldata[i]->gidparenthead[j]);
                    if (numingroup[gidval]>0) papsldata[i]->gidparenthead[j]=&pfofall[pglistall[gidval][0]];
                    else papsldata[i]->gidparenthead[j]=NULL;
                }
                if (papsldata[i]->giduberparenthead[j]!=NULL) {
                    gidval=(*papsldata[i]->giduberparenthead[j]);
                    if (numingroup[gidval]>0) papsldata[i]->giduberparenthead[j]=&pfofall[pglistall[gidval][0]];
                }
                else papsldata[i]->giduberparenthead[j]=NULL;
            }
        }
        }
        //store old number of groups
        ng=ngroupdark;
        //if any structures have been changed need to update the structure pointers. 
        //For now, do NOT reorder group ids based on number of particles that a group is composed of 
        //as need to adjust the hierarchy data structure.
        //before unbinding store the parent and uberparent of an object
        Int_t *nsub,*parentgid,*uparentgid,*stype;
        nsub=new Int_t[ngroupdark+1];
        parentgid=new Int_t[ngroupdark+1];
        uparentgid=new Int_t[ngroupdark+1];
        stype=new Int_t[ngroupdark+1];
        GetHierarchy(opt,ngroupdark,nsub,parentgid,uparentgid,stype);
        if (CheckUnboundGroups(opt,nparts, Part, ngroupdark, pfofall,ningall,pglistall,0)) {
            //must rebuild pglistall 
            for (i=1;i<=ng;i++) delete[] pglistall[i];
            delete[] pglistall;
            pglistall=BuildPGList(nparts, ng, ningall, pfofall);
            //now adjust the structure pointers after unbinding where groups are NOT reordered
            int nlevel=0,ninleveloff=0,ii;
            for (i=1;i<=ng;i++) {
                if (i-ninleveloff>papsldata[nlevel]->nsinlevel) {
                    ninleveloff+=papsldata[nlevel]->nsinlevel;
                    nlevel++;
                }
                ii=i-ninleveloff;
                if (ningall[i]==0) {
                    papsldata[nlevel]->Phead[ii]=NULL;
                    papsldata[nlevel]->gidhead[ii]=NULL;
                    papsldata[nlevel]->gidparenthead[ii]=NULL;
                    papsldata[nlevel]->giduberparenthead[ii]=NULL;
                }
                else {
                    papsldata[nlevel]->Phead[ii]=&Part[pglistall[i][0]];
                    papsldata[nlevel]->gidhead[ii]=&pfofall[pglistall[i][0]];
                    if (parentgid[i]!=GROUPNOPARENT) {
                        if (ningall[parentgid[i]]>0) papsldata[nlevel]->gidparenthead[ii]=&pfofall[pglistall[parentgid[i]][0]];
                        else papsldata[nlevel]->gidparenthead[ii]=NULL;
                    }
                    if (uparentgid[i]!=GROUPNOPARENT) {
                        if (ningall[uparentgid[i]]>0) papsldata[nlevel]->giduberparenthead[ii]=&pfofall[pglistall[uparentgid[i]][0]];
                        else papsldata[nlevel]->giduberparenthead[ii]=NULL;
                    }
                }
            }
            for (i=nhierarchy-1;i>=0;i--) {
                Int_t ninlevel=0;
                for (Int_t k=1;k<=papsldata[i]->nsinlevel;k++) if (papsldata[i]->Phead[k]!=NULL) ninlevel++;
                for (Int_t k=1,j=0;k<=ninlevel;k++) {
                    if (papsldata[i]->Phead[k]==NULL) {
                        j=k+1;while(papsldata[i]->Phead[j]==NULL) j++;
                        //copy the first non-NULL pointer to current NULL pointers,
                        //ie: for a non-existing structure which has been unbound, remove it from the structure list
                        //by copying the pointers of the next still viable structure to that address and setting 
                        //the pointers at the new position, j, to NULL
                        papsldata[i]->Phead[k]=papsldata[i]->Phead[j];
                        papsldata[i]->gidhead[k]=papsldata[i]->gidhead[j];
                        papsldata[i]->gidparenthead[k]=papsldata[i]->gidparenthead[j];
                        papsldata[i]->giduberparenthead[k]=papsldata[i]->giduberparenthead[j];
                        papsldata[i]->stypeinlevel[k]=papsldata[i]->stypeinlevel[j];
                        papsldata[i]->Phead[j]=NULL;
                        papsldata[i]->gidhead[j]=NULL;
                        papsldata[i]->gidparenthead[j]=NULL;
                        papsldata[i]->giduberparenthead[j]=NULL;
                        papsldata[i]->stypeinlevel[j]=BGTYPE;
                    }
                }
                papsldata[i]->nsinlevel=ninlevel;
            }
            //reorder groups just according to number of dark matter particles
            //and whether object is a substructure or not
            numingroup=BuildNumInGroup(ndark, ng, pfofall);
            if (opt.iverbose) cout<<ThisTask<<" Reorder after finding baryons and unbinding!"<<endl;
            //if wish to organise halos and subhaloes differently, adjust numingroup which is used to sort data
            if (ihaloflag) {
                Int_t nleveloffset=nhalos,ninleveloffset=0;
                for (Int_t k=1;k<=papsldata[0]->nsinlevel;k++)ninleveloffset+=ningall[(*papsldata[0]->gidhead[k])];
                for (i=1;i<=nhalos;i++) if (numingroup[i]>0) numingroup[i]+=ninleveloffset;
            }
            //store new number of halos
            nhalos=papsldata[0]->nsinlevel;
            if (iinclusive) ReorderGroupIDsAndHaloDatabyValue(ng, ngroupdark, ningall, pfofall, pglistall, numingroup, pdata);
            else ReorderGroupIDsbyValue(ng, ngroupdark, ningall, pfofall, pglistall, numingroup);
            if (opt.iverbose) cout<<ThisTask<<" Done"<<endl;
            delete[] numingroup;
            delete[] ningall;
            for (i=1;i<=ng;i++) delete[] pglistall[i];
            delete[] pglistall;

        }
        delete[] nsub;
        delete[] parentgid;
        delete[] uparentgid;
        delete[] stype;
    }
    
#ifdef USEMPI
    //if number of groups has changed then update
    if (opt.uinfo.unbindflag) {
    cout<<"MPI thread "<<ThisTask<<" has found "<<ngroupdark<<endl;
    MPI_Allgather(&ngroupdark, 1, MPI_Int_t, mpi_ngroups, 1, MPI_Int_t, MPI_COMM_WORLD);
    //free up memory now that only need to store pfof and global ids
    if (ThisTask==0) {
        int totalgroups=0;
        for (int j=0;j<NProcs;j++) totalgroups+=mpi_ngroups[j];
        cout<<"Total number of groups found is "<<totalgroups<<endl;
    }
    }
#endif

    return pfofall;
}
//@}

/// \name Routines used to determine substructure hierarchy
//@{
Int_t GetHierarchy(Options &opt,Int_t ngroups, Int_t *nsub, Int_t *parentgid, Int_t *uparentgid, Int_t* stype)
{
    if (opt.iverbose) cout<<"Getting Hierarchy "<<ngroups<<endl;
    Int_t ng=0,nhierarchy=1,noffset=0;
    StrucLevelData *ppsldata,**papsldata;
    ppsldata=psldata;
    while (ppsldata->nextlevel!=NULL){nhierarchy++;ppsldata=ppsldata->nextlevel;}
    for (Int_t i=1;i<=ngroups;i++) nsub[i]=0;
    for (Int_t i=1;i<=ngroups;i++) parentgid[i]=GROUPNOPARENT;
    for (Int_t i=1;i<=ngroups;i++) uparentgid[i]=GROUPNOPARENT;
    ppsldata=psldata;
    papsldata=new StrucLevelData*[nhierarchy];
    nhierarchy=0;
    while (ppsldata!=NULL) {papsldata[nhierarchy++]=ppsldata;ppsldata=ppsldata->nextlevel;}
    for (int i=nhierarchy-1;i>=1;i--){
        //store number of substructures
        for (int j=1;j<=papsldata[i]->nsinlevel;j++) {
	    if (papsldata[i]->gidparenthead[j]!=NULL) nsub[*(papsldata[i]->gidparenthead[j])]++;
        }
        //then add these to parent substructure
        for (int j=1;j<=papsldata[i]->nsinlevel;j++) {
	    if (papsldata[i]->gidparenthead[j]!=NULL){
            nsub[*(papsldata[i]->gidparenthead[j])]+=nsub[*(papsldata[i]->gidhead[j])];
            parentgid[*(papsldata[i]->gidhead[j])]=*(papsldata[i]->gidparenthead[j]);
            }
            if (papsldata[i]->giduberparenthead[j]!=NULL) uparentgid[*(papsldata[i]->gidhead[j])]=*(papsldata[i]->giduberparenthead[j]);
            stype[*(papsldata[i]->gidhead[j])]=(papsldata[i]->stypeinlevel[j]);
        }
    }
    //store field structures (top treel level) types
    for (int j=1;j<=papsldata[0]->nsinlevel;j++) {
        stype[*(papsldata[0]->gidhead[j])]=(papsldata[0]->stypeinlevel[j]);
    }
    for (int i=0;i<nhierarchy;i++)papsldata[i]=NULL;
    delete[] papsldata;
    if(opt.iverbose) cout<<"Done"<<endl;
    return nhierarchy;
}

void CopyHierarchy(Options &opt,PropData *pdata, Int_t ngroups, Int_t *nsub, Int_t *parentgid, Int_t *uparentgid, Int_t* stype)
{
    Int_t i,haloidoffset=0;
#ifdef USEMPI
    for (int j=0;j<ThisTask;j++)haloidoffset+=mpi_ngroups[j];
#endif
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(i)
{
    #pragma omp for nowait
#endif
    for (i=1;i<=ngroups;i++) {
        pdata[i].haloid=opt.snapshotvalue+i;
        //if using mpi than ids must be offset
#ifdef USEMPI
        pdata[i].haloid+=haloidoffset;
#endif
        if (parentgid[i]!=GROUPNOPARENT) {
        pdata[i].hostid=opt.snapshotvalue+uparentgid[i];
        pdata[i].directhostid=opt.snapshotvalue+parentgid[i];
#ifdef USEMPI
        pdata[i].hostid+=haloidoffset;
        pdata[i].directhostid+=haloidoffset;
#endif
        }
        else {
        pdata[i].hostid=GROUPNOPARENT;
        pdata[i].directhostid=GROUPNOPARENT;
        }
        pdata[i].numsubs=nsub[i];
    }
#ifdef USEOPENMP
}
#endif
}
//@}

/// \name Routines used for interative search
//@{
///for halo mergers check, get mean velocity dispersion
inline Double_t GetVelDisp(Particle *Part, Int_t numgroups, Int_t *numingroup, Int_t **pglist){
    Int_t i;
    Double_t mt,sigmav=0;
    Coordinate cmval;
    Matrix dispmatrix;
#ifdef USEOPENMP
#pragma omp parallel default(shared) \
private(i,mt,cmval,dispmatrix)
{
#pragma omp for reduction(+:sigmav)
#endif
    for (i=1;i<=numgroups;i++) {
        for (int k=0;k<3;k++) cmval[k]=0.;
        mt=0.;
        for (Int_t j=0;j<numingroup[i];j++) {
            for (int k=0;k<3;k++) cmval[k]+=Part[pglist[i][j]].GetVelocity(k)*Part[pglist[i][j]].GetMass();
            mt+=Part[pglist[i][j]].GetMass();
        }
        mt=1.0/mt;
        for (int k=0;k<3;k++) cmval[k]*=mt;
        for (int k=0;k<3;k++) for (int l=0;l<3;l++) dispmatrix(k,l)=0.;
        for (Int_t j=0;j<numingroup[i];j++) {
            for (int k=0;k<3;k++) for (int l=k;l<3;l++) dispmatrix(k,l)+=Part[pglist[i][j]].GetMass()*(Part[pglist[i][j]].GetVelocity(k)-cmval[k])*(Part[pglist[i][j]].GetVelocity(l)-cmval[l]);
        }
        for (int k=0;k<3;k++) for (int l=k;l<3;l++) dispmatrix(k,l)*=mt;
        for (int k=1;k<3;k++) for (int l=0;l<k;l++) dispmatrix(k,l)=dispmatrix(l,k);
        sigmav+=pow(dispmatrix.Det(),1./3.);
    }
#ifdef USEOPENMP
}
#endif
    sigmav/=(double)numgroups;
    return sigmav;
}

///Create array for each group listing all previously unlinked particles that should now be linked. Note that the triple pointer newIndex is a double pointer, extra pointer layer is to ensure that 
///it is passed by reference so that the memory allocated in the function is associated with the newIndex pointer once the function as returned.
inline void DetermineNewLinks(Int_t nsubset, Particle *Partsubset, Int_t *pfof, Int_t numgroups, Int_t &newlinks, Int_t *newlinksIndex, Int_t *numgrouplinksIndex, Int_t *nnID, Int_t ***newIndex) {
    Int_t pid,ppid;
    newlinks=0;
    for (Int_t j=1;j<=numgroups;j++) numgrouplinksIndex[j]=0;
    for (Int_t j=0;j<nsubset;j++) {
        ppid=Partsubset[j].GetID();
        if (nnID[ppid]!=pfof[ppid]) {
            if (nnID[ppid]>0&&pfof[ppid]==0) {newlinksIndex[newlinks++]=j;numgrouplinksIndex[nnID[ppid]]++;}
        }
    }
    for (Int_t j=1;j<=numgroups;j++) {
        (*newIndex)[j]=new Int_t[numgrouplinksIndex[j]+1];
        numgrouplinksIndex[j]=0;
    }
    for (Int_t j=0;j<nsubset;j++) {
        ppid=Partsubset[j].GetID();
        if (nnID[ppid]>0&&pfof[ppid]==0) {(*newIndex)[nnID[ppid]][numgrouplinksIndex[nnID[ppid]]++]=j;}
    }
}

///Create array for each group listing all possibly intergroup links 
inline void DetermineGroupLinks(Int_t nsubset, Particle *Partsubset, Int_t *pfof, Int_t numgroups, Int_t &newlinks, Int_t *newlinksIndex, Int_t *numgrouplinksIndex, Int_t *nnID, Int_t ***newIndex) {
    Int_t i,pid,ppid;
    //store number of links between groups in newIndex
    for (i=1;i<=numgroups;i++) numgrouplinksIndex[i]=0;
    //first determine number of links a group has with particles identified as belonging to another group 
    for (i=0;i<newlinks;i++) {
        ppid=Partsubset[newlinksIndex[i]].GetID();
        if (nnID[ppid]!=pfof[ppid]&&nnID[ppid]>-1) {numgrouplinksIndex[nnID[ppid]]++;}
    }
    //then go through list and store all these particles
    for (i=1;i<=numgroups;i++) {
        (*newIndex)[i]=new Int_t[numgrouplinksIndex[i]+1];
        numgrouplinksIndex[i]=0;
    }
    for (i=0;i<newlinks;i++) {
        ppid=Partsubset[newlinksIndex[i]].GetID();
        if (nnID[ppid]!=pfof[ppid]&&nnID[ppid]>-1) {(*newIndex)[nnID[ppid]][numgrouplinksIndex[nnID[ppid]]++]=newlinksIndex[i];}
    }
}
///once Group links are found, reduce the data so that one can determine for each group, number of other groups linked, their gids and the number of that group linked
///these are stored in numgrouplinksIndex, intergroupgidIndex, & newintergroupIndex
inline void DetermineGroupMergerConnections(Particle *Partsubset, Int_t numgroups, Int_t *pfof, int *ilflag, Int_t *numgrouplinksIndex, Int_t *intergrouplinksIndex, Int_t *nnID, Int_t ***newIndex, Int_t ***newintergroupIndex, Int_t ***intergroupgidIndex) {
    Int_t i,ii,pid,ppid, intergrouplinks;
    //ilflag ensures that group only assocaited once to current group being searched for intergroup links
    for (i=1;i<=numgroups;i++) ilflag[i]=0;
    for (i=1;i<=numgroups;i++) {
        intergrouplinks=0;
        for (ii=0;ii<numgrouplinksIndex[i];ii++) {
            ppid=Partsubset[(*newIndex)[i][ii]].GetID();
            if (ilflag[pfof[ppid]]==0) {intergrouplinksIndex[intergrouplinks++]=pfof[ppid];ilflag[pfof[ppid]]=1;}
        }
        if (intergrouplinks>0) {
            (*newintergroupIndex)[i]=new Int_t[intergrouplinks];
            (*intergroupgidIndex)[i]=new Int_t[intergrouplinks];
            for (ii=0;ii<intergrouplinks;ii++) {
                (*intergroupgidIndex)[i][ii]=intergrouplinksIndex[ii];
                (*newintergroupIndex)[i][ii]=0;
                ilflag[intergrouplinksIndex[ii]]=0;
            }
            for (ii=0;ii<numgrouplinksIndex[i];ii++) {
                ppid=Partsubset[(*newIndex)[i][ii]].GetID();
                for (Int_t j=0;j<intergrouplinks;j++) if (pfof[ppid]==(*intergroupgidIndex)[i][j]) {(*newintergroupIndex)[i][j]++;}
            }
        }
        numgrouplinksIndex[i]=intergrouplinks;
        delete[] (*newIndex)[i];
    }
}

///Routine uses a tree to mark all particles meeting the search criteria given by the FOF comparison function and the paramaters in param. The particles are marked in nnID in ID order
///Only the set of particles in newlinksIndex are searched for other particles meeting this criteria. Note that the tree search is set up such that if the marker value (here given by pfof) greater than the particle's
///current id marker or the particle's current marker is 0, the particle's nnID value is set to the marker value. 
inline void SearchForNewLinks(Int_t nsubset, KDTree *tree, Particle *Partsubset, Int_t *pfof, FOFcompfunc &fofcmp, Double_t *param, Int_t newlinks, Int_t *newlinksIndex, Int_t **nnID, Double_t **dist2, int nthreads) {
    Int_t i,ii;
    int tid;
#ifdef USEOPENMP
        if (newlinks>ompsearchnum) {
        for (ii=0;ii<nsubset;ii++) for (int j=1;j<nthreads;j++) nnID[0][ii+j*nsubset]=nnID[0][ii];
#pragma omp parallel default(shared) \
private(ii,tid)
{
#pragma omp for schedule(dynamic,1) nowait
        for (ii=0;ii<newlinks;ii++) {
            tid=omp_get_thread_num();
            tree->SearchCriterion(newlinksIndex[ii], fofcmp,param, pfof[Partsubset[newlinksIndex[ii]].GetID()], &nnID[0][tid*nsubset], &dist2[0][tid*nsubset]);
        }
}
#pragma omp parallel default(shared) \
private(ii,tid)
{
#pragma omp for
        for (ii=0;ii<nsubset;ii++) {
            for (int j=1;j<nthreads;j++) {
                if (nnID[0][ii]==0) nnID[0][ii]=nnID[0][ii+j*nsubset];
                else if (nnID[0][ii+j*nsubset]>0) {
                    if (nnID[0][ii+j*nsubset]<nnID[0][ii]) nnID[0][ii]=nnID[0][ii+j*nsubset];
                }
            }
        }
}
        }
        else 
#endif
        {
            tid=0;
            for (ii=0;ii<newlinks;ii++) {
                tree->SearchCriterion(newlinksIndex[ii], fofcmp,param, pfof[Partsubset[newlinksIndex[ii]].GetID()], nnID[tid], dist2[tid]);
            }
        }
}
inline void SearchForNewLinks(Int_t nsubset, KDTree *tree, Particle *Partsubset, Int_t *pfof, FOFcompfunc &fofcmp, Double_t *param, Int_t newlinks, Int_t *newlinksIndex, Int_t **nnID, int nthreads) {
    Int_t i,ii;
    int tid;
#ifdef USEOPENMP
        if (newlinks>ompsearchnum) {
        for (ii=0;ii<nsubset;ii++) for (int j=1;j<nthreads;j++) nnID[0][ii+j*nsubset]=nnID[0][ii];
#pragma omp parallel default(shared) \
private(ii,tid)
{
#pragma omp for schedule(dynamic,1) nowait
        for (ii=0;ii<newlinks;ii++) {
            tid=omp_get_thread_num();
            tree->SearchCriterion(newlinksIndex[ii], fofcmp,param, pfof[Partsubset[newlinksIndex[ii]].GetID()], &nnID[0][tid*nsubset]);
        }
}
#pragma omp parallel default(shared) \
private(ii,tid)
{
#pragma omp for
        for (ii=0;ii<nsubset;ii++) {
            for (int j=1;j<nthreads;j++) {
                if (nnID[0][ii]==0) nnID[0][ii]=nnID[0][ii+j*nsubset];
                else if (nnID[0][ii+j*nsubset]>0) {
                    if (nnID[0][ii+j*nsubset]<nnID[0][ii]) nnID[0][ii]=nnID[0][ii+j*nsubset];
                }
            }
        }
}
        }
        else 
#endif
        {
            tid=0;
            for (ii=0;ii<newlinks;ii++) {
                tree->SearchCriterion(newlinksIndex[ii], fofcmp,param, pfof[Partsubset[newlinksIndex[ii]].GetID()], nnID[tid]);
            }
        }
}

///This routine links particle's stored in newIndex to a group. Routine assumes that each group's list of new particle's is independent.
inline void LinkUntagged(Particle *Partsubset, Int_t numgroups, Int_t *pfof, Int_t *numingroup, Int_t **pglist, Int_t newlinks, Int_t *numgrouplinksIndex, Int_t **newIndex, Int_t *Head, Int_t *Next, Int_t *GroupTail, Int_t *nnID) {
    Int_t i,pindex,tail,pid,ppid;
    if (newlinks>0) {
#ifdef USEOPENMP
#pragma omp parallel default(shared) \
private(i,pindex,tail,pid,ppid)
{
#pragma omp for schedule(dynamic,1) nowait
#endif
        for (i=1;i<=numgroups;i++) if (numgrouplinksIndex[i]>0) {
            pindex=pglist[i][0];
            tail=GroupTail[i];
            for (Int_t k=0;k<numgrouplinksIndex[i];k++) {
                pid=newIndex[i][k];
                ppid=Partsubset[pid].GetID();
                Head[pid]=Head[pindex];
                Next[tail]=pid;
                tail=pid;
                pfof[ppid]=i;
                nnID[ppid]=i;
            }
            numingroup[i]+=numgrouplinksIndex[i];
            GroupTail[i]=newIndex[i][numgrouplinksIndex[i]-1];
        }
#ifdef USEOPENMP
}
#endif
    }
}

///This routine merges candidate substructre groups together so long as the groups share enough links compared to the groups original size given in oldnumingroup
///(which is generally the group size prior to the expanded search).
inline Int_t MergeGroups(Options &opt, Particle *Partsubset, Int_t numgroups, Int_t *pfof, Int_t *numingroup, Int_t *oldnumingroup, Int_t **pglist, Int_t *numgrouplinksIndex, Int_t ***intergroupgidIndex, Int_t ***newintergroupIndex, Int_t *intergrouplinksIndex, Int_t *Head, Int_t *Next, Int_t *GroupTail, int *igflag, Int_t *nnID, Int_t &newlinks, Int_t *newlinksIndex) {
    Int_t i,gid,gidjoin,pindex,lindex,sindex,tail,ss,intergrouplinks,mergers=0;
    for (i=1;i<=numgroups;i++) if (igflag[i]==0&&numgrouplinksIndex[i]>0) {
        intergrouplinks=0;
        for (Int_t j=0;j<numgrouplinksIndex[i];j++) {
            gidjoin=(*intergroupgidIndex)[i][j];
            if (igflag[gidjoin]==0&&i!=gidjoin) {
            //merge if enough links relative to original size of group
            int merge=((*newintergroupIndex)[i][j]>opt.fmerge*oldnumingroup[gidjoin]);
            if (merge) {
                mergers++;
                intergrouplinksIndex[gidjoin]=0;numingroup[gidjoin]=0;
                igflag[gidjoin]=1;
                pindex=pglist[i][0];
                sindex=pglist[gidjoin][0];lindex=pindex;
                gid=pfof[Partsubset[Head[lindex]].GetID()];
                //first adjust length of the larger group
                tail=GroupTail[i];
                //then adjust the next of the tail of the larger group
                Next[tail]=Head[sindex];
                ss=Head[sindex];
                //then adjust the group id, head and group length of the smaller group
                do {
                    pfof[Partsubset[ss].GetID()]=gid;
                    nnID[Partsubset[ss].GetID()]=gid;
                    Head[ss]=Head[lindex];
                    intergrouplinks++;
                    newlinksIndex[newlinks++]=ss;
                } while((ss = Next[ss]) >= 0);
                GroupTail[i]=GroupTail[gidjoin];
                if (numgrouplinksIndex[gidjoin]>0) {delete[] (*newintergroupIndex)[gidjoin];delete[] (*intergroupgidIndex)[gidjoin];}
            }
            }
        }
        numingroup[i]+=intergrouplinks;
        delete[] (*newintergroupIndex)[i];delete[] (*intergroupgidIndex)[i];
    }
    return mergers;
}

///This routine merges candidate halo groups together during the merger check so long as the groups share enough links compared to the groups original size given in oldnumingroup
///or one group is significantly larger than the other (in which case the secondary object is probably a substructure of the other)
inline Int_t MergeHaloGroups(Options &opt, Particle *Partsubset, Int_t numgroups, Int_t *pfof, Int_t *numingroup, Int_t *oldnumingroup, Int_t **pglist, Int_t *numgrouplinksIndex, Int_t ***intergroupgidIndex, Int_t ***newintergroupIndex, Int_t *intergrouplinksIndex, Int_t *Head, Int_t *Next, Int_t *GroupTail, int *igflag, Int_t *nnID, Int_t &newlinks, Int_t *newlinksIndex) {
    Int_t i,gid,gidjoin,pindex,lindex,sindex,tail,ss,intergrouplinks,mergers=0;
    for (i=1;i<=numgroups;i++) if (igflag[i]==0&&numgrouplinksIndex[i]>0) {
        intergrouplinks=0;
        for (Int_t j=0;j<numgrouplinksIndex[i];j++) {
            gidjoin=(*intergroupgidIndex)[i][j];
            if (igflag[gidjoin]==0&&i!=gidjoin) {
            //merge if enough links relative to original size of group
            int merge=(((*newintergroupIndex)[i][j]>opt.fmergebg*oldnumingroup[gidjoin])||((Double_t)oldnumingroup[gidjoin]/(Double_t)oldnumingroup[i]<opt.HaloMergerRatio*opt.fmergebg));
            if (merge) {
                mergers++;
                intergrouplinksIndex[gidjoin]=0;numingroup[gidjoin]=0;
                igflag[gidjoin]=1;
                pindex=pglist[i][0];
                sindex=pglist[gidjoin][0];lindex=pindex;
                gid=pfof[Partsubset[Head[lindex]].GetID()];
                //first adjust length of the larger group
                tail=GroupTail[i];
                //then adjust the next of the tail of the larger group
                Next[tail]=Head[sindex];
                ss=Head[sindex];
                //then adjust the group id, head and group length of the smaller group
                do {
                    pfof[Partsubset[ss].GetID()]=gid;
                    nnID[Partsubset[ss].GetID()]=gid;
                    Head[ss]=Head[lindex];
                    intergrouplinks++;
                    newlinksIndex[newlinks++]=ss;
                } while((ss = Next[ss]) >= 0);
                GroupTail[i]=GroupTail[gidjoin];
                if (numgrouplinksIndex[gidjoin]>0) {delete[] (*newintergroupIndex)[gidjoin];delete[] (*intergroupgidIndex)[gidjoin];}
            }
            }
        }
        numingroup[i]+=intergrouplinks;
        delete[] (*newintergroupIndex)[i];delete[] (*intergroupgidIndex)[i];
    }
    return mergers;
}

//@}