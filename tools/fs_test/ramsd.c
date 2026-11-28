/* RAM-backed SD card + simulated clock for the host filesystem harness. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "sdcard.h"
#include "sdcard_lxr02.h"

uint8_t *ram_img; uint32_t ram_blocks;
uint64_t sim_us;                 /* simulated microseconds */
volatile uint16_t time_sysTick;  /* 1 ms tick, derived */
unsigned long sd_reads, sd_writes;
/* Block timing: LXR-02 SPI SD, about 0.6 ms per read, 1.5 ms per write (estimate). */
#define US_READ 600u
#define US_WRITE 1500u
static void advance(uint32_t us){ sim_us += us; time_sysTick = (uint16_t)(sim_us / 1000u); }
void sim_advance(uint32_t us){ advance(us); }

static int pend; static uint32_t pb; static uint8_t *pbuf; static sdcard_operationCompleteCallback_c pcb; static uint32_t pdata; static sdcardBlockOperation_e pop;

int ram_load(const char *path){
  FILE *f=fopen(path,"rb"); if(!f) return -1; fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
  ram_img=malloc(n); if(fread(ram_img,1,n,f)!=(size_t)n) return -1; fclose(f); ram_blocks=n/512; return 0; }
int ram_save(const char *path){ FILE *f=fopen(path,"wb"); fwrite(ram_img,512,ram_blocks,f); fclose(f); return 0; }

unsigned char SD_init(void){ advance(2000); return 0; }
unsigned char SD_readSingleBlockCustomBuffer(unsigned long b, uint8_t *t){ if(b>=ram_blocks) return 1; memcpy(t,ram_img+512*b,512); sd_reads++; advance(US_READ); return 0; }
void spi_sd_set_fast(void){} void spi_sd_set_slow(void){}

bool sdcard_readBlock(uint32_t b, uint8_t *buf, sdcard_operationCompleteCallback_c cb, uint32_t d){
  if(pend) return false; pend=1; pb=b; pbuf=buf; pcb=cb; pdata=d; pop=SDCARD_BLOCK_OPERATION_READ; return true; }
sdcardOperationStatus_e sdcard_writeBlock(uint32_t b, uint8_t *buf, sdcard_operationCompleteCallback_c cb, uint32_t d){
  if(pend) return SDCARD_OPERATION_BUSY; pend=1; pb=b; pbuf=buf; pcb=cb; pdata=d; pop=SDCARD_BLOCK_OPERATION_WRITE; return SDCARD_OPERATION_IN_PROGRESS; }
bool sdcard_poll(void){
  if(!pend) return true;
  pend=0;
  if(pb>=ram_blocks){ fprintf(stderr,"SD OOR block %u\n",pb); exit(3); }
  if(pop==SDCARD_BLOCK_OPERATION_READ){ memcpy(pbuf,ram_img+512*pb,512); sd_reads++; advance(US_READ); }
  else { memcpy(ram_img+512*pb,pbuf,512); sd_writes++; advance(US_WRITE); }
  if(pcb) pcb(pop,pb,pbuf,pdata);
  return true; }
sdcardOperationStatus_e sdcard_beginWriteBlocks(uint32_t a, uint32_t c){(void)a;(void)c;return SDCARD_OPERATION_SUCCESS;}
sdcardOperationStatus_e sdcard_endWriteBlocks(void){return SDCARD_OPERATION_SUCCESS;}
void sdcard_setProfilerCallback(sdcard_profilerCallback_c c){(void)c;}
uint8_t sdcard_getState(void){ return pend?1:0; }
void sdcard_getTransportSnapshot(sdcardTransportSnapshot_t *s){ memset(s,0,sizeof *s); }
void sdcard_abortTransferForBootLog(void){ pend=0; }

uint32_t timebase_tim2Now(void){ advance(5); return (uint32_t)sim_us; }
uint32_t timebase_tim2Delta(uint32_t a, uint32_t b){ return a-b; }
void timebase_holdPreAudioMs(uint16_t ms){ advance(1000u*ms); }
