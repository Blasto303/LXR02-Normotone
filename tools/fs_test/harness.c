/* Host harness: real Helicase filesystem + data modules on a RAM FAT32 card.
 * usage: harness card.img [bankslot ...]   (boot sequence of main.c, then Bank loads) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "filesystem.h"
#include "SceneData.h"
#include "BankData.h"
#include "InstrumentManager.h"
#include "EffectsManager.h"

extern int ram_load(const char *); extern unsigned long sd_reads, sd_writes; extern unsigned long long sim_us;
static void pump(const char *what){
  unsigned long r0=sd_reads,w0=sd_writes; unsigned long long t0=sim_us; long n=0;
  while(filesystem_status()==FS_STATUS_BUSY){ filesystem_tick(); if(++n>50000000){printf("%s: STUCK\n",what);exit(2);} }
  printf("%-28s status=%d err='%s' reads=%lu writes=%lu sim=%.1f ms\n", what, (int)filesystem_status(),
         filesystem_errorCode()?filesystem_errorCode():"", sd_reads-r0, sd_writes-w0, (sim_us-t0)/1000.0);
}
int main(int argc,char**argv){
  if(ram_load(argv[1])){printf("no image\n");return 1;}
  scene_initAll(); bank_init(); instrumentManager_runtimeInit(); effects_init();
  if(!filesystem_initCardAndMountBlocking()){printf("mount failed\n");return 1;}
  printf("mounted\n");
  filesystem_requestScanKits(NULL); pump("scan kits"); filesystem_ack();
  printf("kit index ok=%d\n", filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_KIT));
  filesystem_requestScanScenes(NULL); pump("scan scenes"); filesystem_ack();
  (void)filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_SCENE);
  filesystem_requestScanBanks(NULL); pump("scan banks"); filesystem_ack();
  (void)filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_BANK);
  filesystem_requestScanPatterns(NULL); pump("scan patterns"); filesystem_ack();
  printf("pattern index ok=%d\n", filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_PATTERN));
  printf("boot index ok=%d\n", (int)filesystem_createBootIndexBlocking());
  filesystem_requestLoadBankIndex(NULL); pump("load bank index"); filesystem_ack();
  int cur_index=-1;
  for(int a=2;a<argc;a++){
    char w[64]; const char *arg=argv[a]; int slot; bool ok;
    char kind = (arg[0]>='0'&&arg[0]<='9') ? 'b' : arg[0];
    slot = atoi(kind=='b'?arg:arg+2);
    if(kind!=cur_index){
      if(kind=='k') ok=filesystem_requestLoadKitIndex(NULL);
      else if(kind=='s') ok=filesystem_requestLoadSceneIndex(NULL);
      else if(kind=='p') ok=filesystem_requestLoadPatternIndex(NULL);
      else ok=filesystem_requestLoadBankIndex(NULL);
      if(ok){ pump("load index"); filesystem_ack(); }
      cur_index=kind;
    }
    snprintf(w,sizeof w,"%c load %03d",kind,slot);
    if(kind=='k') ok=filesystem_requestLoadKitForScenes((uint16_t)slot,1u,NULL);
    else if(kind=='s') ok=filesystem_requestLoadSceneForScenes((uint16_t)slot,1u,NULL);
    else if(kind=='p') ok=filesystem_requestLoadPatternForScenes((uint16_t)slot,1u,NULL);
    else ok=filesystem_requestLoadBank((uint16_t)slot,0xFFFFu,0u,0x02u,NULL);
    if(!ok){printf("%s: request refused\n",w);continue;}
    pump(w);
    if(kind=='b') printf("   failed mask=%04X loaded mask=%04X\n", filesystem_lastBankLoadFailedSceneMask(), filesystem_lastBankLoadSceneMask());
    filesystem_ack();
  }
  return 0;
}
