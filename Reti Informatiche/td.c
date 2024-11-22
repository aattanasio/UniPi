#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define HELP "Comandi:\nmenu -> stampa il menu\ncomanda -> invia una comanda in cucina\n\t\t   NOTA: deve essere nel formato\n \t\t   {<piatto_1-quantità_1>...<piatto_n-quantità_n>}\nconto -> richiesta del conto\n"
# define BENVENUTO "***************************** BENVENUTO *****************************\n Digita un comando:\n1) help --> mostra i dettagli dei comandi\n2) menu --> mostra il menu dei piatti\n3) comanda --> invia una comanda\n4) conto --> chiede il conto\n"
#define BUF_LEN 1024

int main(int argc, char* argv[]){
	int ret, sd, i, len;

	uint16_t lmsg;

	struct sockaddr_in srv_addr;
	char buffer[BUF_LEN];
	char tmp[128];

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

    // aspetto in questo for finché non ho inserito
    // un codice di prenotazione valido
	for(;;){ 
		printf("Codice prenotazione: ");
		fflush(stdout);
		scanf("%s", tmp);
		// invio al server una stringa nel formato
		// "TD <codice>" 
		strcpy(buffer, "TD ");
		strcat(buffer, tmp);

		len = strlen(buffer);
		len++;
		// Gestione endianness (converto in 'network order')
		lmsg = htons(len);
		// Invio al server la dimensione del messaggio
		ret = send(sd, (void*) &lmsg, sizeof(uint16_t), 0);
		
		// Invio il messaggio l'identificazione del kd
		ret = send(sd, (void*) buffer, len, 0);

		// Ricezione lunghezza risposta
		ret = recv(sd, (void*)&lmsg, sizeof(uint16_t), 0);
		strcpy (buffer, "");
		// Attendo la risposta
		ret = recv(sd, (void*)buffer, lmsg, 0);

		if(strcmp(buffer, "END\0") == 0){ // il server si è chiuso
			printf("Server chiuso\n");
			close(sd);
			return 0;
		} else if(strcmp(buffer, "OK\0") == 0){ // il codice era valido
			break;
		} else {
			printf("%s\n", buffer);
		}
	}
	// stampo la schermata di benvenuto
	printf(BENVENUTO);

	for(;;){
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

		// variabili per il controllo dell'input
		char test[1024];
		int npar; // conta il numero di parole inserite
		char* ptest;

		// variabili per l'invio del messaggio
		char work[BUF_LEN];
		char* cmd;
		int len;
		uint16_t lmsg;

		// Spazzolo i descrittori 
		for(i = 0; i <= fdmax; i++){
			// Controllo se l'fd i-esimo è pronto
			fflush(stdout);
			if(FD_ISSET(i, &read_fds)){
				if(i == 0){
					scanf(" %[^\n]", buffer);

					strcpy(test, buffer);
					ptest = strtok(test, " ");
					npar = 0;
					while(ptest != NULL){
						npar++;
						ptest = strtok(NULL, " ");
					}
					// adesso npar contiene il numero di parole

					strcpy(work, buffer); 
					// work contiene la stringa originale
					// buffer contiene la stringa modificata
					// cmd punta alla prima parola di buffer
					cmd = strtok(buffer, " ");

					if((strcmp(cmd, "comanda") == 0 && npar > 1) || 
					   (strcmp(cmd, "menu") == 0 && npar == 1)){ // invio il comando al server

						len = strlen(work);
						len++;
						// Gestione endianness (converto in 'network order')
						lmsg = htons(len);
						// Invio al server la dimensione del messaggio
						ret = send(sd, (void*) &lmsg, sizeof(uint16_t), 0);
						
						// Invio il messaggio
						// che contiene "comanda ..." o "menu"
						ret = send(sd, (void*) work, len, 0);
					} else if(strcmp(cmd, "conto") == 0 && npar == 1){
						len = strlen(work);
						len++;
						// Gestione endianness (converto in 'network order')
						lmsg = htons(len);
						// Invio al server la dimensione del messaggio
						ret = send(sd, (void*) &lmsg, sizeof(uint16_t), 0);
						
						// Invio il messaggio
						// che contiene "conto"
						ret = send(sd, (void*) work, len, 0);

						// Ricezione lunghezza risposta
						ret = recv(sd, (void*)&lmsg, sizeof(uint16_t), 0);
						strcpy (buffer, "");
						// Attendo la risposta
						ret = recv(sd, (void*)buffer, lmsg, 0);
						
						if(strcmp(buffer, "END\0") == 0){
							printf("Server chiuso\n");

						} else {
							printf("%sArrivederci!\n", buffer);
						}

						close(sd);
						return 1;
					} else if(strcmp(cmd, "help") == 0 && npar == 1){
						printf(HELP); // stampo spiegazione dei comandi
					} else {
						printf("COMANDO NON RICONOSCIUTO\n");
					}
				}
				else{ // il socket è sd
					// Ricezione lunghezza risposta
					ret = recv(sd, (void*)&lmsg, sizeof(uint16_t), 0);
					strcpy (buffer, "");
					// Attendo la risposta
					ret = recv(sd, (void*)buffer, lmsg, 0);
					if(ret == 0){ // il socket del client si è chiuso
						printf("Server chiuso\n");
						fflush(stdout);
						close(i);
						return 1;
					}
					if(strcmp(buffer, "END\0") == 0){
						printf("Server chiuso\n");
						close(sd);
						return 0;
					}
					printf("%s\n", buffer);
				}
			}
		}

		
	}
}
