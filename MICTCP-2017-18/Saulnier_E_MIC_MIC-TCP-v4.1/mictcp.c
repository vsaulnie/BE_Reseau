#include <mictcp.h>
#include <api/mictcp_core.h>
#include <string.h>

#define TIMER_WAIT_ACK 100
#define MAX_ENVOI_PDU 10

/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
//Variables suplementaires
mic_tcp_sock s;
int next_fd = 0;
int num_sequence =0;
mic_tcp_sock_addr addr_hote_distant;

#define NBRE_MESSAGES_EXAMINES 5
int pourcentage_pertes_max_admissible=20;
int Tab_Mess[NBRE_MESSAGES_EXAMINES];
int mess_suivant=0;
pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

int Calculer_pertes_courantes(void)
{
  int somme=0;
  int i;
  for(i=0;i<NBRE_MESSAGES_EXAMINES;i++)
	somme+=Tab_Mess[i]*100/NBRE_MESSAGES_EXAMINES;
  return somme;
}



int mic_tcp_socket(start_mode sm) 
{
  //Coté Client
  if (sm == CLIENT) {
    pourcentage_pertes_max_admissible=20;
    printf("Pourcentage de perte désiré côté client : %d\n", pourcentage_pertes_max_admissible);
  }
  //Coté Serveur
  else {
    pourcentage_pertes_max_admissible=10;
    printf("Pourcentage de perte désiré côté serveur : %d\n", pourcentage_pertes_max_admissible);
  }
  
  if(initialize_components(sm)==-1) /* Appel obligatoire */
    {
      return -1;
    }
  else
    {
      s.fd=next_fd++;
      s.state=IDLE;
      set_loss_rate(10);
      int i;
      for(i=0;i<NBRE_MESSAGES_EXAMINES;i++)
	Tab_Mess[i]=1;
    }
   return s.fd;
}

/*
 * Permet d’attribuer une adresse à un socket. 
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr)
{
  if(s.fd==socket && s.state==IDLE)
    {
      memcpy((char *)&s.addr, (char*)&addr, sizeof(mic_tcp_sock_addr));
      s.state=BINDED;
      return 0;
    }
  else
    return -1;
}

/*
 * Met le socket en état d'acceptation de connexions
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr* addr) //addr <=> addresse de celui qui initie le connect
{
    if(s.fd==socket && s.state==BINDED)
      {
	//En attente du SYN (On passe process received PDU en mode reception de SYN)
	s.state=WAIT_FOR_SYN;
	//Bloquage du mutex qui permet d'attendre la reception du syn
	if (pthread_mutex_lock(&mut) != 0)
	  printf("Erreur: pthread_mutex_lock\n");
	//On reste bloqué tant que l'on ne recois pas de SYN
	while(s.state == WAIT_FOR_SYN)
	  pthread_cond_wait(&cond,&mut);
	//On debloque le mutex quand le syn est reçu
	if (pthread_mutex_unlock(&mut) != 0)
	  printf("Erreur: pthread_mutex_unlock\n");
	//------>SYN_RECIVED -> SENDING SYNACK
	//Construction du synack
	mic_tcp_pdu synack;
	synack.header.source_port=s.addr.port;
	synack.header.dest_port=addr->port;
	synack.header.seq_num=pourcentage_pertes_max_admissible;
	synack.header.ack_num=0;
	synack.header.syn=1;
	synack.header.ack=1;
	synack.header.fin=0;
	synack.payload.size=0;
	//Envoi du SYNACK
	if (IP_send(synack, *addr) == -1 )
	  printf("Erreur : IP_send\n");
	
	//En attente du ACK (On passe process received PDU en mode reception d'ACK du Accept)
	s.state=WAIT_FOR_ACK;
	//Bloquage du mutex qui permet d'attendre la reception de l'ack
	if (pthread_mutex_lock(&mut) != 0)
	  printf("Erreur: pthread_mutex_lock\n");
	//On reste bloqué tant que l'on ne recois pas d'ack
	while(s.state == WAIT_FOR_ACK)
	  pthread_cond_wait(&cond,&mut);
	//On debloque le mutex quand l'ack est reçu
	if (pthread_mutex_unlock(&mut) != 0)
	  printf("Erreur: pthread_mutex_unlock\n");
	//------>ACK_RECIVED -> CONNECTED
	s.state=CONNECTED;
	return 0;
      }

    return -1;
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{  
  if(s.fd==socket && s.state==IDLE)
    {
      //Creation du SYN
      mic_tcp_pdu syn;
      syn.header.source_port=s.addr.port;
      syn.header.dest_port=addr.port;
      syn.header.seq_num=0;
      syn.header.ack_num=0;
      syn.header.syn=1;
      syn.header.ack=0;
      syn.header.fin=0;
      syn.payload.data=0;
      //Envoi du SYN tant que le syn ack non reçu
      int r;
      mic_tcp_sock_addr addr_synack;
      mic_tcp_pdu synack;
      int correct_synack_received = 0;
      int nbre_envoi = 0;
      while(!correct_synack_received && nbre_envoi<MAX_ENVOI_PDU)
	{
	  if(IP_send(syn, addr) == -1 )
	    printf("Erreur : IP_send\n");
	  nbre_envoi++;
	  s.state=SYN_SENT;
	  if((r=IP_recv(&synack, &addr_synack, TIMER_WAIT_ACK))>=0) //No Timeout Case
	    {
	      //if PDU is SYNACK passes
	      if(synack.header.syn==1 && synack.header.ack==1 && synack.header.fin==0)
		{
		      correct_synack_received=1;
		}	  
	    }
	}
      if(correct_synack_received)
	{
	  /* Negociation du pourcentage de pertes admissibles */
          /* Si le pourcentage coté serveur est le plus faible on prend ce dernier, sinon on garde celui par défault coté client */
          if(synack.header.seq_num < pourcentage_pertes_max_admissible)
            pourcentage_pertes_max_admissible = synack.header.seq_num;
	  
	  //Construction de l'ACK (avec envoi du pourcentage de pertes admissibles negocie)
          mic_tcp_pdu ack;
	  ack.header.source_port=s.addr.port;
	  ack.header.dest_port=addr.port;
	  ack.header.seq_num=pourcentage_pertes_max_admissible;
	  ack.header.ack_num=0;
	  ack.header.syn=0;
	  ack.header.ack=1;
	  ack.header.fin=0;
	  ack.payload.data=0;

          //Envoi de l'ACK
          if (IP_send(ack, addr) == -1)
	    printf("Erreur : IP_send");

	  s.state=CONNECTED;
	  addr_hote_distant=addr;
	  return 0;
	}
    }
  return -1;
}

/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send (int mic_sock, char* mesg, int mesg_size)
{
  int valeur_retour = -1;
  if(s.fd==mic_sock && s.state==CONNECTED)
    {
      //Creation du PDU
      mic_tcp_pdu p;
      p.header.source_port=s.addr.port;
      p.header.dest_port=addr_hote_distant.port;
      p.header.seq_num=num_sequence;
      p.header.ack_num=0;
      p.header.syn=0;
      p.header.ack=0;
      p.header.fin=0;
      p.payload.data=mesg;
      p.payload.size=mesg_size;
      //Mise à jour numero de sequence
      num_sequence=(num_sequence+1)%2;

      //Determination du Nbre de renvoi maximal
      //Si pertes sur les derniers messages < au pertes max admissibles
      //alors On envoie qu'une fois
      //Sinon On envoie MAX_REENVOI_PDU fois
      int max_envoi_PDU;
      if(Calculer_pertes_courantes()<pourcentage_pertes_max_admissible)
	{
	  max_envoi_PDU=1;
	}
      else
	{
	  max_envoi_PDU=MAX_ENVOI_PDU;
	}
      //Boucle Envoi PDU/ Reception ACK
      int t,r;
      mic_tcp_sock_addr addr_ack;
      mic_tcp_pdu ack;
      int correct_ack_received = 0;
      int nbre_envoi = 0;
      while(!correct_ack_received && nbre_envoi<max_envoi_PDU)
	{
	  t = IP_send(p, addr_hote_distant);
	  nbre_envoi++;
	  s.state=WAIT_FOR_ACK;
	  if((r=IP_recv(&ack, &addr_ack, TIMER_WAIT_ACK))>=0) //No Timeout Case
	    {
	      //if PDU is ACK passes
	      if(ack.header.syn==0 && ack.header.ack==1 && ack.header.fin==0)
		{
		  if(p.header.seq_num==ack.header.ack_num)
		    {
		      correct_ack_received=1;
		      valeur_retour=t;
		    }
		}	  
	    }
	}
      if(max_envoi_PDU==1)
	valeur_retour=t;
      //Mise à jour du tableau des derniers messages envoyés
      Tab_Mess[mess_suivant]=correct_ack_received;
      mess_suivant=(mess_suivant+1)%NBRE_MESSAGES_EXAMINES;
      //Fin de la boucle d'envoi
      s.state=CONNECTED;      
    }
  return valeur_retour;
}

/*
 * Permet à l’application réceptrice de réclamer la récupération d’une donnée 
 * stockée dans les buffers de réception du socket
 * Retourne le nombre d’octets lu ou bien -1 en cas d’erreur
 * NB : cette fonction fait appel à la fonction app_buffer_get() 
 */
int mic_tcp_recv (int socket, char* mesg, int max_mesg_size)
{
    if(s.fd==socket && s.state==CONNECTED)
    {
      s.state=PDU_WAITING;
      mic_tcp_payload p;
      p.data=mesg;
      p.size=max_mesg_size;
      int taille_recv = app_buffer_get(p);
      s.state=CONNECTED;
      return taille_recv;
    }
    else
      return -1;
}

/*
 * Permet de réclamer la destruction d’un socket. 
 * Engendre la fermeture de la connexion suivant le modèle de TCP. 
 * Retourne 0 si tout se passe bien et -1 en cas d'erreur
 */
int mic_tcp_close (int socket)
{
  if(s.fd==socket && s.state!=CLOSING)
    {
      //Construction PDU FIN

      //Envoi PDU avec Timer

      //Etat : Closing
      
      //Attente de FIN ACK
      //Reception FIN ACK
      //ENvoi de ACK
      // etat : CLOSED
      s.state=CLOSED;
      return 0;
    }
  else
    return -1;
}

/*
 * Traitement d’un PDU MIC-TCP reçu (mise à jour des numéros de séquence
 * et d'acquittement, etc.) puis insère les données utiles du PDU dans 
 * le buffer de réception du socket. Cette fonction utilise la fonction 
 * app_buffer_put().   
 */
void process_received_PDU(mic_tcp_pdu pdu, mic_tcp_sock_addr addr)
{
  printf("s.state=>%d",s.state);
  if(s.state==PDU_WAITING)
    {
      if(pdu.header.syn==0 && pdu.header.ack==0 && pdu.header.fin==0)
	{
	  //Message Received if on sequence put in buffer
	  if(pdu.header.seq_num==num_sequence)
	    {
	      app_buffer_put(pdu.payload);
	      num_sequence=(num_sequence+1)%2;
	    }
	  //send ACK any case
	  mic_tcp_pdu ack;
	  ack.header.source_port=s.addr.port;
	  ack.header.dest_port=pdu.header.source_port;
	  ack.header.seq_num=0;
	  ack.header.ack_num=pdu.header.seq_num;
	  ack.header.syn=0;
	  ack.header.ack=1;
	  ack.header.fin=0;
	  ack.payload.size=0;
	  IP_send(ack, addr);
	}
      else
	{
	  //Message reçu non traitable
	  printf("process_received_PDU : Invalid PDU Header at this time || Exepcting a message PDU\n");
	}
    }
  else if (s.state==WAIT_FOR_SYN)
    {
      //We received a SYN
      if (pdu.header.syn == 1 && pdu.header.ack==0 && pdu.header.fin==0)
	{
	  if (pthread_cond_broadcast(&cond) != 0)
	    {
	      printf("Erreur: pthread_cond_broadcast\n");
	    }
	  //Now sending SYNACK via mic_tcp_accept
	  s.state = WAIT_FOR_ACK;
	  if (pthread_mutex_unlock(&mut) !=0 )
	    {
	      printf("Erreur: pthread_mutex_unlock\n");
	    }
	}
      else
	{
	  //Message reçu non traitable
	  printf("process_received_PDU : Invalid PDU Header at this time || Exepcting a SYN\n");
	}
    }
  else if (s.state==WAIT_FOR_ACK)
    {
      if (pdu.header.syn == 0 && pdu.header.ack==1 && pdu.header.fin==0)
	{
	  /* On recupere le pourcentage de perte via le numero de sequence */
	  pourcentage_pertes_max_admissible = pdu.header.seq_num;

	  if (pthread_cond_broadcast(&cond) != 0)
	    {
	      printf("Erreur: pthread_cond_broadcast\n");
	    }
	  s.state = CONNECTED;
      
	  if (pthread_mutex_unlock(&mut) !=0 )
	    {
	      printf("Erreur: pthread_mutex_unlock\n");
	    }
	}
      else
	{
	  //Message reçu non traitable
	  printf("process_received_PDU : Invalid PDU Header at this time || Exepcting an ACK\n");
	}
    }
  else
    {
      //Message reçu non attendu à ce moment
      printf("process_received_PDU : PDU received not expected\n");
    }
  

}
