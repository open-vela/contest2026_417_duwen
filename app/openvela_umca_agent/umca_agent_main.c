#include <stdio.h>

#include "service/umca_service.h"

int main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  if (umca_service_start() != 0)
    {
      printf("UMCA service failed to start.\n");
      return 1;
    }
  printf("UMCA service is running.\n");
  return 0;
}
