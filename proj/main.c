#include "forensic.h"
#include "options.h"

int main(int argc, char* argv[]){

    if (argc < 2) {
        fprintf( stderr, "Usage: %s dir_name\n", argv[0]);
        exit(1);
    }

    struct options options;
    options.fp_mask = 0;

    if(parse_options(argc, argv, &options) == 1)
        exit(2);

    struct stat file_stats;

    if(stat(argv[argc-1],&file_stats) != 0)
        return -1;
    
    //build_file_line();



    get_file_info(argv[argc - 1]);
    

    exit(0);
}