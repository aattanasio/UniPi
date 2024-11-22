#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define BUF_LEN 1024 // dimensione del buffer
#define END "END\0"  // messaggio di terminazione inviato dal server
// menu dei comandi del server
#define COMANDI_SRV "COMANDI DISPONIBILI:\n1) stat ->  restituisce le comande del giorno per tavolo o per status ('a', 'p' o 's') \n2) stop ->  il server si arresta (solo se non ci sono comande in attesa o in preparazione\n" 

#define N_PIATTI 8 		// numero di piatti diversi nel menù
#define N_TAVOLI 12 	// 12 tavoli in tutto il ristorante
#define MAX_PREN_TAV 10 // ipotesi: numero massimo di prenotazioni per quel tavolo
#define MAX_COM_TAV 10 	// ipotesi: numero massimo di comande per quel tavolo
#define MAX_KD 5		// ipotesi: numero di cuochi

#define END_LEN 4 // "END\0" messaggio di terminaizone
#define OK_LEN 3  // "OK\0"	 messaggio di risposta ad un codice di prenotazione valido

// struttura per le entrate del menù
struct entryMenu{
	char id[2];
	int prezzo;
};

// vettore per il menù
struct entryMenu menu[N_PIATTI];

// struttura per i piatti all'interno delle comande
struct Piatto{
	char id[2];
	int q; 
};

enum stato_com{none, in_attesa, in_preparazione, in_servizio};
struct Comanda{
	enum stato_com stato;
	int kd; 	// kitchen device che se ne sta occupando
	struct Piatto piatti[N_PIATTI]; // vettore di piatti ordinati nella comanda
	time_t timestamp; //timestamp della comanda
};

struct Tavolo{ // i tavoli vanno da 0 a N_TAVOLI-1
	int posti;
	char sala[16];	 // SALA1 - SALA2 - SALA3 - SALA4
	char descr[16];  // CAMINO - FINESTRA
};

struct Tavolo tavoli[N_TAVOLI];

struct Prenotazione{
	int codice_pren;
	char data_ora[11]; // GG-MM-AA HH
};

// associa un socket id ad un table device
// e alla prenotazione che sta servendo.
// Per identificare la prenotazione, memorizzo
// l'indice di colonna della matrice delle prenotazioni
// (l'indice di colonna è dato dal numero del tavolo)
struct Tds{
	int sd;
	int c_pren;
};

// array di id (di socket relativi ai table device)
// indicizzato dal numero di tavolo
struct Tds tds[N_TAVOLI];  // posso avere al più un table device per ogni tavolo

int kds[MAX_KD]; 	// posso avere al più un kitchen device per ogni cuoco
int last_kd = 0;	// indice libero nell'array dei kitchen device

int com_attesa = 0; // mantiene costantemente il numero di comande in attesa

struct Prenotazione prenotazioni[N_TAVOLI][MAX_PREN_TAV];

// struttura per gestire le comande
// indice di riga 	 = id tavolo
// indice di colonna = id comanda
struct Comanda comande[N_TAVOLI][MAX_COM_TAV];

// inizializza la struttura per 
// l'associazione socket-tavolo
void init_tds(){
	int i;
	for(i = 0; i < N_TAVOLI; i++){
		tds[i].sd = -1;
		tds[i].c_pren = -1;
	}
}

/*
- input: stato delle comande che si vuole stampare
- output: nessuno
- funzionamento: stampa a video tutte le comande, 
  compresi i piatti che le compongono, che hanno 
  lo stato specificato come input
*/
void stampa_stato_com(int stato){
	int tavolo, comanda, piatto;
	struct Comanda *com;
	for(tavolo = 0; tavolo < N_TAVOLI; tavolo++){
		for(comanda = 0; comanda < MAX_COM_TAV; comanda++){
			com = &comande[tavolo][comanda];
			if(com->stato == stato){
				printf("com%d T%d\n", comanda, tavolo);
				fflush(stdout); 
				for(piatto = 0; piatto < N_PIATTI; piatto++){
					if(com->piatti[piatto].q > 0){
						printf("%s %d\n", com->piatti[piatto].id, com->piatti[piatto].q);
						fflush(stdout); 
					}
				}
			}
		}
	}
}

/*
- input: numero tavolo
- output: nessuno
- funzionamento: stampa a video tutte le comande, 
  compresi i piatti che le compongono, inviate dal
  tavolo specificato come input
*/
void stampa_com_tav(int tavolo){
	int comanda, piatto;
	struct Comanda *com;
	char *str;
	
	for(comanda = 0; comanda < MAX_COM_TAV; comanda++){
		com = &comande[tavolo][comanda];
		
		switch(com->stato){
			case in_attesa:
				str = "<in attesa>";
				break;
			case in_preparazione:
				str = "<in preparazione>";
				break;
			case in_servizio:
				str = "<in servizio>";
				break;
			default:
				continue;
		}
		printf("com%d %s\n", comanda, str);
		fflush(stdout); 
		for(piatto = 0; piatto < N_PIATTI; piatto++){
			if(com->piatti[piatto].q > 0){
				printf("%s %d\n", com->piatti[piatto].id, com->piatti[piatto].q);
				fflush(stdout); 
			}
		}
		
	}
}

/*
- input: nessuno
- output: 0 se il server non può terminare, 1 altrimenti
- funzionamento: restituisce 1 se esiste almeno una 
  comanda in attesa o in preparazione
*/
int can_stop(){
	int tavolo, comanda;
	struct Comanda *com;
	for(tavolo = 0; tavolo < N_TAVOLI; tavolo++){
		for(comanda = 0; comanda < MAX_COM_TAV; comanda++){
			com = &comande[tavolo][comanda];
			if(com->stato != none && com->stato != in_servizio)
				return 0;
		}
	}
	return 1;
}

/*
- input: nessuno
- output: nessuno
- funzionamento: inserisce nella struttura dati 'tavoli'
  il parsing del file tavoli.txt
*/
void pars_tavoli(){
	FILE *fptr;
	int i;
	char *work;
	char str[1024];
	fptr = fopen("tavoli.txt","r");
	if (fptr == NULL){
       printf("Error! opening file tavoli.txt");
       exit(1);
   	}
   	for(i = 0; i < N_TAVOLI; i++){
   		fgets(str, sizeof(str), fptr);
   		work = strtok(str, " ");
   		tavoli[i].posti = atoi(work);
   		work = strtok(NULL, " ");
   		strcpy(tavoli[i].sala, work);
   		work = strtok(NULL, " ");
   		strcpy(tavoli[i].descr, work);
   	}

	fclose(fptr);
}
/*
- input: nessuno
- output: nessuno
- funzionamento: inserisce nella struttura dati 'menu'
  il parsing del file menu.txt
*/
void pars_menu(){
	FILE *fptr;
	int i;
	char str[1024];
	char *work;
	fptr = fopen("menu.txt","r");
	if (fptr == NULL){
       printf("Error! opening file menu.txt");
       exit(1);
   	}

   	for(i = 0; i < N_PIATTI; i++){
   		fgets(str, sizeof(str), fptr);
   		work = strtok(str, " ");
   		strcpy(menu[i].id, work);
   		work = strtok(NULL, "€");
   		work = strtok(NULL, "€");
   		menu[i].prezzo = atoi(work);
   	}

	fclose(fptr);
}

/*
- input: nessuno
- output: nessuno
- funzionamento: inizializza a valori non signiicativi
  la struttura dati che serve per gestire le prenotazioni
*/
void clc_pren(){
	int tp, dp;
	struct Prenotazione *p;
	for(tp = 0; tp < N_TAVOLI; tp++){
		for(dp = 0; dp < MAX_PREN_TAV; dp++){
			p = &prenotazioni[tp][dp];
			p->codice_pren = -1;
			strcpy(p->data_ora, "GG-MM-AA HH");
		}
	}
}

/*
- input: numero del tavolo
- output: nessuno
- funzionamento: cancella tutte le comande relative
  ad un tavolo
*/
void clc_com_tav(int tc){ 
	int pi, nc;
	struct Comanda *c;
	for(nc = 0; nc < MAX_COM_TAV; nc++){
		c = &comande[tc][nc];
		c->kd = 0;
		c->stato = none;
		for(pi = 0; pi < N_PIATTI; pi++){
			c->piatti[pi].q = 0;
		}
	}	
}

/*
- input: numero del tavolo
- output: nessuno
- funzionamento: cancella tutte le comande di tutti i tavoli
*/
void clc_com(){
	int tc;
	for(tc = 0; tc < N_TAVOLI; tc++)
		clc_com_tav(tc);
}

/*
- input: riferimento ad una stringa
- output: nessuno
- funzionamento: scrive nella stringa data come input
  il menù salvato nel fine menu.txt
*/
void stampa_menu(char* str){
	FILE *fptr;
	int i;
	char work[1024];
	fptr = fopen("menu.txt","r");
	if (fptr == NULL){
       printf("Error! opening file menu.txt");
       exit(1);
   	}

   	for(i = 0; i < N_PIATTI; i++){
   		fgets(work, sizeof(work), fptr);
   		strcat(str, work);
   	}

	fclose(fptr);
}

/*
- input: codice di prenotazione
- output: numero del tavolo associato alla prenotazione
- funzionamento: restituisce il numero del tavolo associato
  alla prenotazione, -1 se il codice dato come input non è valido.
  Un codice in input non è valido se non esiste nella tabella
  delle prenotazioni o la data a cui si riferisce la prenotazione
  è diversa da quella del momento dell'inserimento.
*/
int is_pren_valid(int codice_pren){ 
	time_t ltime;
	ltime = time(NULL);
	struct tm *tm;
	tm = localtime(&ltime);
	int tp, dp;
	struct Prenotazione *p;
	char timestamp[12];

	// aggiungo 1 ai mesi perché localtime ha i mesi 0-based
	// sottraggo 100 agli anni perché mi conta gli anni trasforsi dal 1900
	sprintf(timestamp,"%02d-%02d-%02d %02d", 
		  tm->tm_mday, tm->tm_mon+1, 
		  tm->tm_year-100, tm->tm_hour);

	for(tp = 0; tp < N_TAVOLI; tp++){
		for(dp = 0; dp < MAX_PREN_TAV; dp++){
			p = &prenotazioni[tp][dp];
			if(p->codice_pren == codice_pren && 
				strcmp(p->data_ora, timestamp) == 0){
				tds[tp].c_pren = dp; // salvo indice di colonna
				return tp;
			}
		}
	}
	return -1;
}

/*
- input: riferimento all'indice del tavolo e all'indice della comanda
- output: riferimento alla comanda da preparare
- funzionamento: cerca tra le comande (in attesa) quella in attesa da più tempo e
  ne restituisce il riferimento. Modifica i valori di input
  scrivendoci rispettivamente l'indice del tavolo e della comanda
  restituita all'interno della struttura 'comande'. Se i due indici
  sono -1, vuol dire che non ci sono comande disponibili, quindi la struttura
  restituita ha un valore non significativo
*/
struct Comanda* find_comanda(int* t, int* c){
	int tav, n_com;
	struct Comanda *com, *prima;
	struct Comanda p;
	p.timestamp = time(NULL);
	prima = &p;
	*t = -1;
	*c = -1;
	for(tav = 0; tav < N_TAVOLI; tav++){
		for(n_com = 0; n_com < MAX_COM_TAV; n_com++){
			com = &comande[tav][n_com];
			if(com->stato == in_attesa && 
			   com->timestamp < prima->timestamp){
				prima = com;
				*t = tav;
				*c = n_com;
			}
		}
	}
	return prima;
}

/*
- input: stringa da modificare e indice del kitchen device
  di cui voglio stampare le comande 
- output: nessuno
- funzionamento: inserisce nella stringa passata come input
  tutte le comande del kitchen device specificato
*/
void com_kd(char str[], int kd){
	int tav, com, pi;
	struct Comanda *c;
	char tmp[128];

	for(tav = 0; tav < N_TAVOLI; tav++){
		for(com = 0; com < MAX_COM_TAV; com++){
			c = &comande[tav][com];
			if(c->stato == in_preparazione && c->kd == kd){
				sprintf(tmp, "com%d T%d\n", com, tav);
				strcat(str, tmp);
				for(pi = 0; pi < N_PIATTI; pi++){
					if(c->piatti[pi].q > 0){
						sprintf(tmp, "%s %d\n", c->piatti[pi].id, c->piatti[pi].q);
						strcat(str, tmp);
					}
				}
			}
		}
	}
}

/*
- input: stringa che contiene una comanda nel formato <piatto-comanda>
  e due riferimenti a interi
- output: nessuno
- funzionamento: scrive negli interi passati per riferimento
  rispettivamente l'id del piatto e la quantità dello stesso nella comanda
*/
void pars_com(char* str, char *piatto, int *quanti){
	char tmp[64];
	char* p;
	strcpy(tmp, str);
	p = strtok(tmp, "-");
	strcpy(piatto, p);
	p = strtok(NULL, "-");
	*quanti = atoi(p);
}