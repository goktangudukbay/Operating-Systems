/*
 * Producer program
 *
 * MUSTAFA GOKTAN GUDUKBAY, 21801740, CS342, SECTION 3
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include "string.h"

int main(int argc, char *argv[])
{
  //alphanumeric array
  char alphanumeric[] = "0123456789AaaBbCcDdEeFfGgHhIiJjKkLlMmNnOoPpQqRrSsTtUuVvWwXxYyZz";
  int alphanumericLength = strlen(alphanumeric);

  //m is the characters to print
  int m = atoi(argv[1]);
  for(int i = 0; i < m; i++)
    printf("%c", alphanumeric[rand() % alphanumericLength]);

}
