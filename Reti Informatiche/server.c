#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "const.h"



int main(int argc, char* argv[]){
	struct sockaddr_in my_addr, cl_addr;
	int ret, newfd, listener, addrlen, i, j, len;
	uint16_t cmdlen;
	uint16_t lmsg;
	char buffer[BUF_LEN];
	char* work;
	int port = atoi(argv[1]);

	// Set di descrittori da monitorare
	fd_set master;
	// Set dei descrittori pronti
	fd_set read_fds;
	// Descrittore max 
	int fdmax;      

	// funzione di utilità per fare il parsing
	// del file contenente le informazioni sui tavoli 
	pars_tavoli(); 
	// funzione di utilità per fare il parsing
	// del file contenente le informazioni sul menu
	pars_menu();
	// funzione di utilità per resettare
	// la struttura delle prenotazioni
	clc_pren();
	// funzione di utilità per resettare
	// la struttura delle comande
	clc_com();
	// funzione di utilità per resettare
	// il vettore che mantiene i socket id
	// dei table device
	init_tds();

	srand(time(NULL)); 

	/* Creazione socket d'ascolto*/
    listener = socket(AF_INET, SOCK_STREAM, 0);

    /* Creazione indirizzo di bind */
    memset(&my_addr, 0, sizeof(my_addr)); 
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(port);
    my_addr.sin_addr.s_addr = INADDR_ANY;

    ret = bind(listener, (struct sockaddr*)&my_addr, sizeof(my_addr) );

    if( ret < 0 ){
        perror("Bind non riuscita\n");
        exit(0);
    }

    listen(listener, 10);

    // Reset FDs
    FD_ZERO(&master);
    FD_ZERO(&read_fds);

    // Aggiungo il socket di ascolto (listener), creato dalla socket() 
    // all'insieme dei descrittori da monitorare (master)
    FD_SET(listener, &master); 

    // Voglio monitorare anche stdin (descrittore 0)
    FD_SET(0, &master); 

	// Aggiorno il massimo (sicuramente listener in quanto stdin è 0)
    fdmax = listener;   

    // Stampo la stringa riguardante i comandi del server
    printf(COMANDI_SRV);  
	fflush(stdout); 

	// Main loop
	while(1){
		// Inizializzo il set read_fds, manipolato dalla select()
        read_fds = master;

        // Mi blocco in attesa di descrottori pronti in lettura
		// imposto il timeout a infinito
		// Quando select() si sblocca, in &read_fds ci sono solo
		// i descrittori pronti in lettura!
        ret = select(fdmax+1, &read_fds, NULL, NULL, NULL);

		if(ret < 0){
			perror("ERRORE SELECT:");
			exit(-1);
		}

		// Spazzolo i descrittori 
		for(i = 0; i <= fdmax; i++){
			// Controllo se l'fd i-esimo è pronto
			if(FD_ISSET(i, &read_fds)){
				if(i == listener){ // è arrivata una richiesta di connessione
					addrlen = sizeof(cl_addr);
					// faccio accept() e creo il socket connesso 'newfd'
                    newfd = accept(listener, (struct sockaddr *)&cl_addr, (socklen_t*)&addrlen);
					// Aggiungo il descrittore al set dei socket monitorati
                    FD_SET(newfd, &master); 
					// Aggiorno l'ID del massimo descrittore
					if(newfd > fdmax){ 
						fdmax = newfd; 
					}
				} 
				// Il socket è stdin
				else if(i == 0) { 
					// Lettura comando dal server
					char* cmd;

					// variabili per il controllo dell'input
					char test[1024];
					int npar; // conta il numero di parole inserite
					char* ptest;

					scanf(" %[^\n]", buffer);

					strcpy(test, buffer);
					ptest = strtok(test, " ");
					npar = 0;
					while(ptest != NULL){
						npar++;
						ptest = strtok(NULL, " ");
					}
					// adesso npar contiene il numero di parole

					cmd = strtok(buffer, " ");
					if(strcmp(buffer, "stop") == 0 && npar == 1){ 
						if(can_stop()){
							printf("Chiusura del server in corso\n");
							fflush(stdout);
							// invio a tutti i socket di comunicazione il messaggio 'END\0'
							for(j = 1; j <= fdmax; j++){ 
								if(FD_ISSET(j, &master) && j != listener){
									// invio messaggio chiusura server
									strcpy(buffer,"END\0");
									// Gestione endianness (converto in 'network order')
									len = END_LEN;
									lmsg = htons(len);
									// Invio al client la dimensione del messaggio
									ret = send(j, (void*) &lmsg, sizeof(uint16_t), 0);
									// Invio il messaggio
									ret = send(j, (void*) buffer, len, 0);
									if(ret < 0){
										perror("Errore in fase di invio comando: \n");
										exit(1);
									}
									close(j);
									FD_CLR(j, &master);
								}
							}
							close(listener);
							
							return 1;
						} else {
							printf("Impossibile chiudere il server\nServire le comande in preparazione e in attesa!\n");
							fflush(stdout);
						}
						
					}
					else if(strcmp(buffer, "stat") == 0 && npar == 2){
						cmd = strtok(NULL, " ");
						if(cmd == NULL){
							continue;
						}
						if(strcmp(cmd, "a\0") == 0)
							stampa_stato_com(in_attesa);
						else if(strcmp(cmd, "p\0") == 0)
							stampa_stato_com(in_preparazione);
						else if(strcmp(cmd, "s\0") == 0)
							stampa_stato_com(in_servizio);
						else{
							// estrarre indice del tavolo 
							// dalla stringa del comando
							int tav_index;
							char *tav = cmd + 1;
							tav_index = atoi(tav);
							stampa_com_tav(tav_index);
						}
					}
					else{
						printf("COMANDO NON RICONOSCIUTO\n");
						fflush(stdout);
					}
				} 
				// se non è il listener e nemmeno stdin, 'i'' è un descrittore di socket 
				// connesso che ha fatto la richiesta di un servizio
				else {
					// ricezione della lunghezza del comando
					ret = recv(i, (void*)&cmdlen, sizeof(uint16_t), 0);
					if(ret == 0){ // il socket del client si è chiuso
						int index, td, kd;
						td = 0;
						kd = 0;
						// se il socket è in tds, è relativo ad un table device
						for(index = 0; index < N_TAVOLI; index++)
							if(tds[index].sd == i){
								td = 1;
								break;
							} 
						// se il socket è in kds è relativo ad un kitchen device
						for(index = 0; index < last_kd; index++)
							if(kds[index] == i){
								kd = 1;
								break;
							}
						if(td == 1){
							printf("Si è chiuso il socket del table device\n");
							fflush(stdout);
						} else if (kd == 1){
							printf("Si è chiuso il socket del kitchen device\n");
							fflush(stdout);
						} else {
							printf("Si è chiuso il socket client\n");
							fflush(stdout);
						}
						
						close(i);
						FD_CLR(i, &master);
						continue;
					}
					// ricezione del comando
					ret = recv(i, (void*)buffer, cmdlen, 0);
					work = strtok(buffer, " ");
					if(strcmp(buffer, "find") == 0){ // operazione del client
						printf("Si è collegato un nuovo client\n");
						fflush(stdout);
						char pren[11] = "GG-MM-AA HH";
						int n_posti;
						int n_disp = 0;
						int tp, dp; // tavolo prenotazione e data prenotazione
						
						printf("Ho letto il comando e ne faccio il parsing\n");
						fflush(stdout);

						// work è nel formato
						// <nome posti GG-MM-AA HH>
						work = strtok(NULL, " "); // scarto il nome
						work = strtok(NULL, " "); // prendo i posti
						n_posti = atoi(work);
						work = strtok(NULL, "\n");
						strcpy(pren, work);

						struct Prenotazione *p;

						printf("Controllo disponibilità\n");
						fflush(stdout);
						// array che mantiene i tavoli disponibili
						int disp[N_TAVOLI] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
						int occupato = 0;
						for(tp = 0; tp < N_TAVOLI; tp++){
							// controllo che il tavolo abbia i posti richiesti
							if(tavoli[tp].posti < n_posti){
								continue;
							} 
							// controllo che il tavolo non sia già occupato
							// in quella data
							for(dp = 0; dp < MAX_PREN_TAV; dp++){
								p = &prenotazioni[tp][dp];
								if(strcmp(p->data_ora, pren) == 0){
									occupato = 1;
								}
							}
							// se è occupato proseguo con la verifica del
							// prossimo tavolo
							if(occupato == 1){
								occupato = 0;
								continue;
							}
							// controllo che il tavolo non abbia raggiunto
							// il massimo di prenotazioni 
							for(dp = 0; dp < MAX_PREN_TAV; dp++){
								p = &prenotazioni[tp][dp];
								if(p->codice_pren == -1){
									disp[n_disp] = tp;
									n_disp++;
									break;
								}
							}
						}

						if(n_disp == 0){
							printf("Non ci sono tavoli liberi\n");
							fflush(stdout);
							strcpy(buffer, "Impossibile prenotare: non ci sono tavoli liberi\0");
							len = strlen(buffer);
							len++;
							// Gestione endianness (converto in 'network order')
							lmsg = htons(len);
							// Invio al client la dimensione del messaggio
							ret = send(i, (void*) &lmsg, sizeof(uint16_t), 0);
							// Invio il messaggio
							ret = send(i, (void*) buffer, len, 0);
							continue;
						}
						// mostro le opzioni disponibili
						printf("Invio opzioni disponibili\n");

						fflush(stdout);
						strcpy(buffer, "");
						int cnt = 1;
						struct Tavolo *tav;
						char opzs[128] = "";
						strcpy(buffer, "Opzioni:\n");
						// creo la stringa delle opzioni disponibili ,
						// nella quale ciascuna opzione è nella forma:
						// n) <tavolo> <sala> <descrizione>
						for(tp = 0; tp < n_disp; tp++){
							tav = &tavoli[disp[tp]];

							if(disp[tp] >= 0){
								sprintf(opzs, "%d) T%d %s %s", cnt, disp[tp], tav->sala, tav->descr);
								strcat(buffer, opzs);
								cnt++;
							}
						}
						len = strlen(buffer);
						len++;
						// Gestione endianness (converto in 'network order')
						lmsg = htons(len);
						// Invio al client la dimensione del messaggio
						ret = send(i, (void*) &lmsg, sizeof(uint16_t), 0);
						// Invio il messaggio
						ret = send(i, (void*) buffer, len, 0);

					} else if(strcmp(buffer, "book") == 0){ // operazione del client
						// comando nella forma:
						// book <tavolo> <data> <ora>
						int codice_pren, tp, dp, occupato;
						codice_pren = rand();
						struct Prenotazione *p;
						work = strtok(NULL, " ");
						tp = atoi(work); // prendo il numero del tavolo
						char pren[11] = "GG-MM-AA HH";
						work = strtok(NULL, " "); // prendo la data
						strcpy(pren, work);
						work = strtok(NULL, " "); // prendo l'ora
						sprintf(pren, "%s %s", pren, work);

						printf("Controllo che il tavolo non sia stato prenotato tra la find e la book\n");
						fflush(stdout);
						
						occupato = 0;
						for(dp = 0; dp < MAX_PREN_TAV; dp++){
							p = &prenotazioni[tp][dp];
							if(strcmp(p->data_ora, pren) == 0){
								occupato = 1;
								break;
							}
						}
						if(occupato == 1){
							printf("Richiesta prenotazione di un tavolo già prenotato\n");
							fflush(stdout);
							sprintf(buffer, "Impossibile prenotare: tavolo %d non più disponibile", tp);
							len = strlen(buffer);
							len++;
							// Gestione endianness (converto in 'network order')
							lmsg = htons(len);
							// Invio al client la dimensione del messaggio
							ret = send(i, (void*) &lmsg, sizeof(uint16_t), 0);
							// Invio il messaggio
							ret = send(i, (void*) buffer, len, 0);
							continue;
						}

						printf("Salvo informazioni sulla prenotazione\n");
						fflush(stdout);
						// salvo la prenotazione nella prima posizione libera
						// nella matrice delle prenotazioni nella riga del 
						// tavolo specificato
						for(dp = 0; dp < MAX_PREN_TAV; dp++){
							if(strcmp(prenotazioni[tp][dp].data_ora, "GG-MM-AA HH") == 0){
								strcpy(prenotazioni[tp][dp].data_ora, pren);
								prenotazioni[tp][dp].codice_pren = codice_pren;
								break;
							}
						}
						printf("Invio al client la conferma della prenotazione\n");
						fflush(stdout);
						sprintf(buffer, "PRENOTAZIONE CONFERMATA\nCodice prenotazione: %d", codice_pren);
						len = strlen(buffer);
						len++;
						// Gestione endianness (converto in 'network order')
						lmsg = htons(len);
						// Invio al client la dimensione del messaggio
						ret = send(i, (void*) &lmsg, sizeof(uint16_t), 0);
						// Invio il messaggio
						ret = send(i, (void*) buffer, len, 0);
						continue;
					} else if(strcmp(buffer, "TD") == 0){ // il table device si è identificato come tale
						int cod;
						int tav;
						// il prossimo campo è il codice prenotazione
						work = strtok(NULL, " "); 
						cod = atoi(work);

						// se il codice è valido, tav contiene 
						// l'id del tavolo relativo al table device
						tav = is_pren_valid(cod);
						if(tav >= 0){
							strcpy(buffer, "OK\0");
							printf("Si è collegato un nuovo table device\n");
							fflush(stdout);
							tds[tav].sd = i; // socket del table device
						} else {
							strcpy(buffer, "Codice prenotazione non valido\0");
							printf("Codice prenotazione non valido\n");
							fflush(stdout);
						}


						len = strlen(buffer);
						len++;
						// Gestione endianness (converto in 'network order')
						lmsg = htons(len);
						// Invio al client la dimensione del messaggio
						ret = send(i, (void*) &lmsg, sizeof(uint16_t), 0);
						// Invio il messaggio
						ret = send(i, (void*) buffer, len, 0);
						continue;
						
					} else if(strcmp(buffer, "menu") == 0){ // comando del table device
						strcpy(buffer, "");
						stampa_menu(buffer);
						len = strlen(buffer);
						len++;
						// Gestione endianness (converto in 'network order')
						lmsg = htons(len);
						// Invio al client la dimensione del messaggio
						ret = send(i, (void*) &lmsg, sizeof(uint16_t), 0);
						// Invio il messaggio
						ret = send(i, (void*) buffer, len, 0);
						continue;
					} else if(strcmp(buffer, "comanda") == 0){ // comando del table device
						char str[128];
						char p[2];
						int com, tc, pi, q, index;
						work = strtok(NULL, " -"); 
						// trovo il tavolo relativo al table device
						for(tc = 0; tc < N_TAVOLI; tc++){
							if(tds[tc].sd == i) break;
						}
						// trovo la prima comanda libera nella
						// matrice delle comande nella riga del tavolo tc
						for(com = 0; com < MAX_COM_TAV; com++)
							if(comande[tc][com].stato == none)
								break;
						// faccio il parsing della comanda
						while(work != NULL){							
							strcpy(p, work);
							work = strtok(NULL, " -");
							q = atoi(work);
							work = strtok(NULL, " -");
							printf("comanda-piatto: %s %d\n", p, q);
							fflush(stdout);
							
							for(pi = 0; pi < N_PIATTI; pi++)
								if(comande[tc][com].piatti[pi].q == 0)
									break;
							comande[tc][com].piatti[pi].q = q;
							strcpy(comande[tc][com].piatti[pi].id, p);
							comande[tc][com].stato = in_attesa;
							comande[tc][com].timestamp = time(NULL);
						}
						com_attesa++; // incremento il contatore delle comande in attesa
						strcpy(str, "");
						strcpy(buffer, "");
						for(index = 0; index < com_attesa; index++)
							strcat(str,"*");
						strcpy(buffer, str);
						len = strlen(buffer);
						len++;
						// Gestione endianness (converto in 'network order')
						lmsg = htons(len);
						printf("Invio notifica a tutti i kitchen device\n");
						fflush(stdout);
						for(index = 0; index < last_kd; index++){
							// Invio al client la dimensione del messaggio
							ret = send(kds[index], (void*) &lmsg, sizeof(uint16_t), 0);
							// Invio il messaggio
							ret = send(kds[index], (void*) buffer, len, 0);
						}
						// Confermo al table device la ricezione della comanda
						printf("Invio conferma al table device\n");
						fflush(stdout);
						strcpy(buffer, "COMANDA RICEVUTA");
						len = strlen(buffer);
						len++;
						// Invio al client la dimensione del messaggio
						ret = send(i, (void*) &lmsg, sizeof(uint16_t), 0);
						// Invio il messaggio
						ret = send(i, (void*) buffer, len, 0);
					} else if(strcmp(buffer, "conto") == 0){ // comando del table device
						int com;
						int tc;
						int pc, q, pm;
						int conto = 0;  // conto totale
						int conto_parz; // importo del piatto * quantità dello stesso
						struct Comanda *c;
						char tmp[BUF_LEN];
						// trovo il tavolo associato al table device
						for(tc = 0; tc < N_TAVOLI; tc++){
							if(tds[tc].sd == i) break;
						}
						strcpy(buffer, "");
						// calcolo del conto
						for(pm = 0; pm < N_PIATTI; pm++){
							conto_parz = 0;
							q = 0;
							for(com = 0; com < MAX_COM_TAV; com++){
								c = &comande[tc][com];
								for(pc = 0; pc < N_PIATTI; pc++){
									if(c->piatti[pc].q > 0 && strcmp(c->piatti[pc].id, menu[pm].id) == 0){
										q += c->piatti[pc].q;
										conto_parz += c->piatti[pc].q * menu[pm].prezzo;
									}
								}
							}
							// salto i piatti che non sono stati ordinati
							if(q == 0) continue; 
							sprintf(tmp, "%s %d %d\n", menu[pm].id, q, conto_parz);
							strcat(buffer, tmp);
							conto += conto_parz;
						}
						// cancello prenotazione
						int cp = tds[tc].c_pren;
						prenotazioni[tc][cp].codice_pren = -1;
						strcpy(prenotazioni[tc][cp].data_ora, "GG-MM-AA HH");
						tds[tc].c_pren = -1;

						printf("Invio del conto al table device\n");
						fflush(stdout);
						sprintf(tmp, "Totale: €%d\n", conto);
						strcat(buffer, tmp);
						
						len = strlen(buffer);
						len++;
						// Gestione endianness (converto in 'network order')
						lmsg = htons(len);
						// Invio al client la dimensione del messaggio
						ret = send(i, (void*) &lmsg, sizeof(uint16_t), 0);
						printf("Ho inviato la lunghezza\n");
						fflush(stdout);
						// Invio il messaggio
						ret = send(i, (void*) buffer, len, 0);

						// cancello tutte le comande di quel tavolo
						clc_com_tav(tc);
						tds[tc].sd = -1;
						printf("Chiusura socket con table device");
						fflush(stdout);
						close(i);
						FD_CLR(i, &master);

					} else if(strcmp(buffer, "KD") == 0){ // il kitche device si è identificato come tale
						printf("Si è collegato un nuovo kitchen device\n");
						fflush(stdout);
						kds[last_kd++] = i; // socket del kitchen device
						continue;
					} else if(strcmp(buffer, "take") == 0){ // comando del kitchen device
						printf("Il kitchen device ha fatto la take\n");
						fflush(stdout);
						int t, c, p;
						char tmp[128];
						struct Comanda *target = find_comanda(&t, &c);
						if(t == -1 && c == -1){
							sprintf(buffer, "Non ci sono comande disponibili");
							printf("Non ci sono comande disponibili\n");
							fflush(stdout);
						} else {
							target->kd = i;
							target->stato = in_preparazione;
							// comunico al table device che la
							// comanda è stata presa in carico
							strcpy(buffer, "COMANDA IN PREPARAZIONE");
							len = strlen(buffer);
							len++;
							printf("Notifica della presa in carica della comanda al table device\n");
							fflush(stdout);
							// Gestione endianness (converto in 'network order')
							lmsg = htons(len);
							// Invio al client la dimensione del messaggio
							ret = send(tds[t].sd, (void*) &lmsg, sizeof(uint16_t), 0);
							// Invio il messaggio
							ret = send(tds[t].sd, (void*) buffer, len, 0);
							printf("Invio al kitchen device il contenuto della comanda\n");
							fflush(stdout);
							strcpy(buffer, "");
							sprintf(buffer, "com%d T%d\n", c, t);
							com_attesa--; // mantiene il numero di comande in attesa in ogni momento
						
							for(p = 0; p < N_PIATTI; p++){
								if(target->piatti[p].q > 0){
									sprintf(tmp, "%s %d\n", target->piatti[p].id, target->piatti[p].q);
									strcat(buffer, tmp);
								}
							}

							len = strlen(buffer);
							len++;
							// Gestione endianness (converto in 'network order')
							lmsg = htons(len);
							// Invio al client la dimensione del messaggio
							ret = send(i, (void*) &lmsg, sizeof(uint16_t), 0);
							// Invio il messaggio
							ret = send(i, (void*) buffer, len, 0);
						}

					} else if(strcmp(buffer, "show") == 0){ // comando del kitchen device
						printf("Il kitchen device ha fatto la show\n");
						fflush(stdout);
						strcpy(buffer, "");
						// funzione di utilità per scrivere in una stringa 
						// le comande accettate da quel table device
						com_kd(buffer, i);

						len = strlen(buffer);
						len++;
						// Gestione endianness (converto in 'network order')
						lmsg = htons(len);
						// Invio al client la dimensione del messaggio
						ret = send(i, (void*) &lmsg, sizeof(uint16_t), 0);
						// Invio il messaggio
						ret = send(i, (void*) buffer, len, 0);
					} else if(strcmp(buffer, "ready") == 0){ // comando del kitchen device
						printf("Il kitchen device ha fatto la ready\n");
						fflush(stdout);
						char str[8];
						int tav, com;
						
						// faccio il parsing del comando <comanda-tavolo>
						work = strtok(NULL, " ");
						strcpy(str, work);
						work = strtok(str, "m");
						work = strtok(NULL, "-");
						com = atoi(work);

						work = strtok(NULL, "T");
						tav = atoi(work);

						//cambio lo stato della comanda specificata
						comande[tav][com].stato = in_servizio;

						printf("La comanda com%d del T%d è in servizio\n", com, tav);
						fflush(stdout);
						// Invio la conferma al kitche device
						sprintf(buffer, "COMANDA IN SERVIZIO");
						len = strlen(buffer);
						len++;
						// Gestione endianness (converto in 'network order')
						lmsg = htons(len);
						// Invio al client la dimensione del messaggio
						ret = send(i, (void*) &lmsg, sizeof(uint16_t), 0);
						// Invio il messaggio
						ret = send(i, (void*) buffer, len, 0);
						continue;
					} else { 
						printf("COMANDO NON RICONOSCIUTO\n");
						fflush(stdout);
					}	
				}
			}
		} 
	}
	return 1;         
}