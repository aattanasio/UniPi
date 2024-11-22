#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define COMANDI "find --> ricerca la disponibilità per una prenotazione\nbook --> invia una prenotazione\nesc --> termina il client\n"

#define BUF_LEN 1024
#define N_TAVOLI 12 // 12_tavoli nel ristorante

int main(int argc, char* argv[]){
	int ret, sd, i;

	struct sockaddr_in srv_addr;
	char buffer[BUF_LEN];

	// Set di descrittori da monitorare
	fd_set master;
	// Set dei descrittori pronti
	fd_set read_fds;
	// Descrittore max 
	int fdmax;

	/* Creazione socket */
	sd = socket(AF_INET,SOCK_STREAM,0);
	
	/* Creazione indirizzo del server */
	memset(&srv_addr, 0, sizeof(srv_addr));
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port = htons(4242);
	inet_pton(AF_INET, "127.0.0.1", &srv_addr.sin_addr);
	
	/* connessione */
	ret = connect(sd, (struct sockaddr*)&srv_addr, sizeof(srv_addr));
	
	if(ret < 0){
		perror("Errore in fase di connessione: \n");
		exit(1);
	}

	// Reset FDs
    FD_ZERO(&master);
    FD_ZERO(&read_fds);

    // Aggiungo il socket di comunicazione (sd), creato dalla socket() 
    // all'insieme dei descrittori da monitorare (master)
    FD_SET(sd, &master); 
    // Voglio monitorare anche stdin (descrittore 0)
    FD_SET(0, &master); 

	// Aggiorno il massimo (sicuramente sd in quanto stdin è 0)
    fdmax = sd; 

    // Stampo i comandi che il client può digitare
    printf(COMANDI);

    // serve per mantenere l'associazione opzione-tavolo dopo la find
	int disp[N_TAVOLI] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

	// stringa per salvare la prenotazione
	char pren[128]; // salva tutto il comando find

	for(;;){
		// Inizializzo il set read_fds, manipolato dalla select()
        read_fds = master;

        // Mi blocco in attesa di descrittori pronti in lettura
		// imposto il timeout a infinito
		// Quando select() si sblocca, in &read_fds ci sono solo
		// i descrittori pronti in lettura!
        ret = select(fdmax+1, &read_fds, NULL, NULL, NULL);

		if(ret < 0){
			perror("ERRORE SELECT:");
			exit(-1);
		}

		// variabili per il controllo dell'input
		char test[1024];
		int npar; // conta il numero di parole inserite
		char* ptest;

		// variabili per l'invio del messaggio
		char work[1024];
		char* cmd;
		int len;
		uint16_t lmsg;

		// Spazzolo i descrittori 
		for(i = 0; i <= fdmax; i++){
			// Controllo se l'fd i-esimo è pronto
			if(FD_ISSET(i, &read_fds)){
				if(i == 0){ // sono in stdin

					scanf(" %[^\n]", buffer);
					strcpy(work, buffer);

					strcpy(test, buffer);
					ptest = strtok(test, " ");
					npar = 0;
					while(ptest != NULL){
						npar++;
						ptest = strtok(NULL, " ");
					}
					// adesso npar contiene il numero di parole

					cmd = strtok(buffer, " ");
					
					if(strcmp(buffer, "find") == 0 && npar == 5){
						len = strlen(work);
						len++;
						// Gestione endianness (converto in 'network order')
						lmsg = htons(len);
						// Invio al server la dimensione del messaggio
						ret = send(sd, (void*) &lmsg, sizeof(uint16_t), 0);
						
						// Invio il messaggio
						// che contiene 'find nome posti data ora'
						ret = send(sd, (void*) work, len, 0);
						
						// salvo data e ora prenotazione che mi serviranno 
						// per effettuare la prenotazione con la book
						cmd = strtok(NULL, " "); // scarto il nome 
						cmd = strtok(NULL, " "); // scarto i posti
						cmd = strtok(NULL, " "); // contiene la data
						strcpy(pren, cmd);
						cmd = strtok(NULL, " "); // contiene l'ora
						sprintf(pren, "%s %s", pren, cmd);
						// adesso pren è nella forma GG-MM-AA HH
						
					} else if(strcmp(buffer, "book") == 0 && npar == 2){
						cmd = strtok(NULL, " "); //cmd contiene il numero dell'opzione
						int opz = atoi(cmd);
						opz--;
						// verifico l'opzione selezionata sia associata al tavolo
						if(disp[opz] < 0){
							printf("Impossibile selezionare l'opzione specificata\n");
							fflush(stdout);
							continue;
						}
						// invio al server una stringa del tipo:
						// book <tavolo> <GG-MM-AA> <HH>
						sprintf(buffer, "book %d %s", disp[opz], pren);
						len = strlen(buffer);
						len++;
						// Gestione endianness (converto in 'network order')
						lmsg = htons(len);
						// Invio al server la dimensione del messaggio
						ret = send(sd, (void*) &lmsg, sizeof(uint16_t), 0);
						// Invio il messaggio
						ret = send(sd, (void*) buffer, len, 0);
					} else if(strcmp(buffer, "esc") == 0 && npar == 1){
						close(sd);
						return 1;
					} else {
						printf("COMANDO NON RICONOSCIUTO\n");
						fflush(stdout);
					}
				}
				else { // il socket è sd
					
					// Ricezione lunghezza risposta
					ret = recv(i, (void*)&lmsg, sizeof(uint16_t), 0);
					if(ret == 0){ // il socket del client si è chiuso
						printf("Server chiuso\n");
						fflush(stdout);
						close(i);
						return 0;
					}
					// Attendo la risposta
					ret = recv(i, (void*)buffer, lmsg, 0);
					strcpy(work, buffer);
					cmd = strtok(buffer, ":");

					if(strcmp(buffer, "END\0") == 0){
						printf("Server chiuso\n");
						close(i);
						FD_CLR(i, &master);
						return 0;
					} else if (strcmp(buffer, "Opzioni\0") == 0){
						printf("%s\n", work);
						cmd = strtok(NULL, ":");
						// parsing opzioni
						char line[32], tmp[1024];
						int index;
						
						strcpy(tmp, cmd);
						char* l = strtok(tmp, "\n");
						char* p;
						int i = 0;
						int j;
						while(l != NULL){
							strcpy(line, l);
							p = strtok(line, ") T");
							index = atoi(p);
							index--;
							p = strtok(NULL, ") T");
							disp[index] = atoi(p);
							i++;
							j = i;
							strcpy(tmp, cmd);
							l = strtok(tmp, "\n");
							while(j > 0 && l != NULL){
								l = strtok(NULL, "\n");
								j--;
							}
						}
					} else {
						printf("%s\n", work);
					}
				}
			}
		}
	}
}