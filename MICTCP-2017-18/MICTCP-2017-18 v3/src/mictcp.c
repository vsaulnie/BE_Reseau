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
	s.state=CONNECTED;
	return 0;
      }
    else
      {
	return -1;
      }
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{  
  if(s.fd==socket && s.state==IDLE)
    {
      s.state=CONNECTED;
      addr_hote_distant=addr;
      return 0;
    }
  else
    {
      return -1;
    }
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
	  s.state=ACK_WAITING;
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
  /* else if(pdu.header.syn==0 && pdu.header.ack==1 && pdu.header.fin==0) */
  /*   { */
  /*     //ACK Received */
  /*     if(pdu.header.ack==num_sequence) */
  /* 	{ */
  /* 	  num_sequence++; */
  /* 	} */
  /*   } */
  /* else if(pdu.header.syn==1 && pdu.header.ack==0 && pdu.header.fin==0) */
  /*   { */
  /*     //SYN Received */
  /*     num_sequence=pdu.header.seq_num; */
  /*     s.state=SYN_RECEIVED; */
  /*   } */
  /* else if(pdu.header.syn==1 && pdu.header.ack==1 && pdu.header.fin==0) */
  /*   { */
  /*     num_sequence=pdu.header.seq_num; */
  /*     //SYNACK Received */
  /*   } */
  /* else if(pdu.header.syn==0 && pdu.header.ack==0 && pdu.header.fin==1) */
  /*   { */
  /*     //FIN Received */
  /*   } */
  /* else if(pdu.header.syn==0 && pdu.header.ack==1 && pdu.header.fin==1) */
  /*   { */
  /*     //FINACK Received */
  /*   } */
  /* else  */
  /*   { */
  /*     //Message reçu non traitable */
  /*     printf("process_received_PDU : Invalid PDU Header\n"); */
  /*   } */
  

}
