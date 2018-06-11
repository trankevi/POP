#include "netbuffer.h"
#include "mailuser.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 1024
#define MAX_ADDRESS_LENGTH 50

static void handle_client(int fd);

int main(int argc, char *argv[]) {
  
  if (argc != 2) {
    fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
    return 1;
  }
  
  run_server(argv[1], handle_client);
  
  return 0;
}

void handle_client(int fd) {
  
  send_string(fd, "+OK POP3 server ready \r\n");

  net_buffer_t nb = nb_create(fd, MAX_LINE_LENGTH);
  char out[MAX_LINE_LENGTH];
  int linebytes = nb_read_line(nb, out);

  int enteruser = 0;
  int authorization = 0;

  char username[MAX_ADDRESS_LENGTH];
  char password[MAX_ADDRESS_LENGTH];

  mail_list_t maillist;
  int mailcount;
  size_t mailsize;

  while (linebytes != 0){

    //replace \r,\n\,or space with \0 for strcmp. save so we can restore later
    char fourthchar = out[4];
    out[4] = '\0';

    
    //AUTHORIZATION state
    //USER,PASS,QUIT


    if (strcasecmp(out, "USER") == 0 &&
	authorization == 0){
      out[4] = fourthchar;

      if (out[4] != ' ' ||
	  out[5] == '\r'){
	send_string(fd, "-ERR no username provided \r\n");
      }
      else {
	int outlen = strlen(out); 
	int userlen = outlen - 2;
	
	memset(username, 0, sizeof username);
	strncpy(username, out + 5, userlen - 5);

	if (is_valid_user(username, NULL) != 0){
	  enteruser = 1;
	  send_string(fd, "+OK mailbox for %s found. Enter PASS command or QUIT. \r\n", username);
	}
	else {
	  send_string(fd, "-ERR invalid username: %s. Please try again \r\n", username);
	  memset(username, 0, sizeof username);
	}
      }
    }


    else if (strcasecmp(out, "PASS") == 0 &&
	     authorization == 0){
      out[4] = fourthchar;      

      if (enteruser == 0){
	send_string(fd, "-ERR please enter USER first \r\n");
      }

      else {
	if (out[4] != ' ' ||
	    out[5] == '\r'){
	  send_string(fd, "-ERR no password provided \r\n");
	}
	else {
	  int outlen = strlen(out);
	  int passlen = outlen - 2;
	  
	  memset(password, 0, sizeof password);
	  strncpy(password, out + 5, passlen - 5);
	  
	  if (is_valid_user(username, password) != 0){
	    authorization = 1;
	    maillist = load_user_mail(username);
	    mailcount = get_mail_count(maillist);
	    mailsize = get_mail_list_size(maillist);

	    send_string(fd, "+OK %s's maildrop has %i messages (%zu octets) \r\n",
			username, mailcount, mailsize);
	  }
	  else {
	    send_string(fd, "-ERR invalid password for user: %s. Please re-enter USER \r\n", username);
	    memset(username, 0, sizeof username);
	    memset(password, 0, sizeof password);
	  }
	}
      }
    }


    else if (strcasecmp(out, "QUIT") == 0 &&
	     fourthchar == '\r'){
      if (authorization == 1){
      destroy_mail_list(maillist);
      }
      send_string(fd, "+OK quit received. Goodbye. \r\n");
      break;
    }


    //TRANSACTION state
    //STAT, NOOP, LIST, RETR, DELE, RSET


    else if (strcasecmp(out, "STAT") == 0 &&
	     fourthchar == '\r' &&
	     authorization == 1){
      mailcount = get_mail_count(maillist);
      mailsize = get_mail_list_size(maillist);
      send_string(fd, "+OK %i %zu \r\n", mailcount, mailsize);
    }


    else if (strcasecmp(out, "NOOP") == 0 &&
	     fourthchar == '\r' &&
	     authorization == 1){
      send_string(fd, "+OK noop \r\n");
    }


    else if (strcasecmp(out, "LIST") == 0 &&
	     authorization == 1){
      mail_item_t mail;
      size_t size;

      if (fourthchar == '\r'){
	send_string(fd, "+OK %i messages (%zu octets) \r\n", mailcount, mailsize);
	if (mailcount == 0){
	  send_string(fd,".\r\n");
	}  
	else {
	  for (int i = 0; i < mailcount; i++) {
	    mail = get_mail_item(maillist, i);
	    if (mail != NULL){
	      size = get_mail_item_size(mail);
	      send_string(fd, "%i %zu \r\n", i+1, size); 
	    }
	  }
	  send_string(fd, ".\r\n");
	}
      }
      
      else {
	out[4] = fourthchar;
	char *number = strchr(out, ' ');
	number++;
	int mailnum = atoi(number);
	mail = get_mail_item(maillist, mailnum-1);
	
	if (mail != NULL){
	  size = get_mail_item_size(mail);
	  send_string(fd, "+OK %i %zu \r\n", mailnum, size);
	}
	
	else {
	  send_string(fd, "-ERR no such message, only %i messages in maildrop \r\n", mailcount);
	}
      }
    }


    else if (strcasecmp(out, "RETR") == 0 &&
             authorization == 1){
      if (fourthchar != ' ') {
	send_string(fd, "-ERR please specify message number to retrieve\r\n");
      }
      else {
	mail_item_t mail;
	out[4] = fourthchar;
	char *number = strchr(out, ' ');
	number++;
	int mailnum = atoi(number);
	mail = get_mail_item(maillist, mailnum-1);
	
	if (mail != NULL){
	  const char *filename = get_mail_item_filename(mail);
	  FILE *openmail = fopen(filename, "r");
	  char line[MAX_LINE_LENGTH];
	  int linelen;

	  send_string(fd, "+OK message follows \r\n");
	  while (line != NULL){
	    if (fgets(line, sizeof line, openmail) == NULL){
	      break;
	    }
	    else {
	      linelen = strlen(line);
	      line[linelen - 1] = '\0';
	      send_string(fd, "%s\r\n", line);
	    }
	  }
	  send_string(fd, ".\r\n");
	  fclose(openmail);
	}

	else {
	  send_string(fd, "-ERR no such message \r\n");
	}
      }
    }

    
    else if (strcasecmp(out, "DELE") == 0 &&
             authorization == 1){
      if (fourthchar != ' ') {
        send_string(fd, "-ERR please specify message number to delete \r\n");
      }
      else {
	mail_item_t mail;
        out[4] = fourthchar;
        char *number = strchr(out, ' ');
        number++;
        int mailnum = atoi(number);
        mail = get_mail_item(maillist, mailnum-1);
	
	if (mail != NULL){
	  mark_mail_item_deleted(mail);
	  send_string(fd, "+OK message %i deleted \r\n", mailnum);
	}

	else {
	  send_string(fd, "-ERR message %i already deleted \r\n", mailnum);
	}
      }
    }

    
    else if (strcasecmp(out, "RSET") == 0 &&
             fourthchar == '\r' &&
             authorization == 1){
      int recovered = reset_mail_list_deleted_flag(maillist);
      mailcount = get_mail_count(maillist);
      mailsize = get_mail_list_size(maillist);
      send_string(fd, "+OK %i messages recovered. Maildrop has %i messages (%zu octets) \r\n",
		  recovered, mailcount, mailsize);
    }


    else {
      out[4] = fourthchar;
      send_string(fd, "-ERR following command not recognized: %s", out);
    }

    linebytes = nb_read_line(nb,out);
  }

  //when QUIT received, gets here by break
  close(fd);

}
