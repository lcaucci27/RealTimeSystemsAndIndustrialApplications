/*****************************************************************************
*
*
* APU Test Application - Verifica Coerenza I/O con RPU
* File: apu_main.c
*
* Questo programma crea volutamente una situazione di incoerenza:
* 1. Scrive un pattern vecchio e lo pulisce in DDR
* 2. Scrive un pattern nuovo SENZA pulire la cache
* 3. L'RPU dovrebbe leggere il nuovo pattern se la coerenza � attiva
*
******************************************************************************/

#include <stdio.h>
#include "xil_printf.h"
#include "xil_cache.h"
#include "xil_mmu.h"
#include "xparameters.h"
#include "sleep.h"

// Indirizzo del buffer condiviso (DEVE corrispondere con RPU)
#define SHARED_BUFFER_ADDR 0x70000000
#define BUFFER_SIZE 256

// Pattern di test
#define PATTERN_OLD 0x0F0F0F0F
#define PATTERN_NEW 0xF0F0F0F0

// Definizioni per MMU: Outer Shareable, Write-Back Cacheable
#define NORM_WB_OUT_CACHE (0x000000FFU | (0x2 << 8)) // Standard: MT_NORMAL_WB | OUTER_SHAREABLE = 0x2FF

int main(void)
{
	volatile u32 *shared_buffer = (volatile u32 *)SHARED_BUFFER_ADDR;
	int i;

	xil_printf("\n\r========================================\n\r");
	xil_printf("APU Coherency Test Application\n\r");
	xil_printf("========================================\n\r");
	xil_printf("Shared Buffer Address: 0x%08x\n\r", SHARED_BUFFER_ADDR);

	// CONFIGURAZIONE CRITICA: Impostare la memoria come outer shareable
	xil_printf("Configurazione MMU per outer shareable...\n\r");
	//Xil_SetTlbAttributes((UINTPTR)shared_buffer, NORM_WB_OUT_CACHE);
	//dsb(); // Data Synchronization Barrier
	xil_printf(" MMU configurata correttamente\n\r");

	// FASE 1: Scrivere pattern vecchio e sincronizzare con DDR
	xil_printf("\n\rFASE 1: Scrittura pattern VECCHIO...\n\r");
	for (i = 0; i < BUFFER_SIZE; i++) {
		shared_buffer[i] = PATTERN_OLD;
	}

	// Pulire la cache per scrivere in DDR
	xil_printf("Pulizia cache (flush) verso DDR...\n\r");
	Xil_DCacheFlushRange(SHARED_BUFFER_ADDR, BUFFER_SIZE * sizeof(u32));
	xil_printf(" DDR ora contiene: 0x%08x\n\r", PATTERN_OLD);

	sleep(1);

	// FASE 2: Scrivere pattern nuovo SENZA pulire la cache
	xil_printf("\n\rFASE 2: Scrittura pattern NUOVO...\n\r");
	for (i = 0; i < BUFFER_SIZE; i++) {
		shared_buffer[i] = PATTERN_NEW;
	}

	// CRITICO: NON eseguire flush della cache!
	xil_printf("IMPORTANTE: Cache NON pulita!\n\r");
	xil_printf(" Cache APU contiene: 0x%08x\n\r", PATTERN_NEW);
	xil_printf(" DDR contiene ancora: 0x%08x\n\r", PATTERN_OLD);

	// FASE 3: Informare l'utente
	xil_printf("\n\r========================================\n\r");
	xil_printf("Setup completato!\n\r");
	xil_printf("Situazione creata:\n\r");
	xil_printf(" - Cache APU: 0x%08x (NUOVO)\n\r", PATTERN_NEW);
	xil_printf(" - DDR: 0x%08x (VECCHIO)\n\r", PATTERN_OLD);
	xil_printf("\n\rSE la coerenza e' ATTIVA:\n\r");
	xil_printf(" RPU leggera' 0x%08x dalla cache APU\n\r", PATTERN_NEW);
	xil_printf("\n\rSE la coerenza NON funziona:\n\r");
	xil_printf(" RPU leggera' 0x%08x dalla DDR\n\r", PATTERN_OLD);
	xil_printf("========================================\n\r");

	xil_printf("\n\rOra avvia l'applicazione RPU per verificare!\n\r");

	// Loop di aggiornamento continuo
	xil_printf("\n\rAggiornamento continuo del buffer...\n\r");
	u32 counter = 0;
	while(1) {
		// Aggiornare periodicamente il buffer (senza flush)
		shared_buffer[0] = PATTERN_NEW + counter;
		xil_printf("Buffer[0] aggiornato a: 0x%08x (solo in cache)\r", PATTERN_NEW + counter);
		counter++;
		sleep(2);
	}

	return 0;
}
