
/*
*       USAGE This routine is C callable and can be called as
* 
*             void gouraud(unsigned int n, unsigned int rd, unsigned int r, 
*                          unsigned int gd, unsigned int g, unsigned int bd, 
*                          unsigned int b, int p[])
*
*             n   --- half of the pixels on a line              (input)
*             rd  --- increment/decrement of the red color      (input)
*             r   --- red color intensity                       (input)
*             gd  --- increment/decrement of the green color    (input)
*             g   --- green color intensity                     (input)
*             bd  --- increment/decrement of the blue color     (input)
*             b   --- blue color intensity                      (input)
*             p[] --- array of pixel's color intensity          (output)
*
*             If the routine is not to be used as a C callable function,
*             then all instructions relating to stack should be removed.
*             Refer to comments of individual instructions. You will also
*             need to initialize values for all the values passed as these
*             are assumed to be in registers as defined by the calling
*             convention of the compiler, (refer to the C compiler reference
*             guide.)
*
*       C CODE
*             This is the C equivalent of the Assembly Code without the 
*             assumptions listed below. Note that the assembly code is hand
*             optimized and assumptions apply.
*
*             void gouraud(unsigned int n, unsigned int rd, unsigned int r, 
*                          unsigned int gd, unsigned int g, unsigned int bd, 
*                          int b, int p[])
*             {
*                 unsigned int    mask = 0xF800F800, i;
*                 for (i = 0; i < n; i++) {
*                   r += rd;
*                   g += gd;
*                   b += bd;
*                   p[i] = (r & mask) + ((g & mask) >> 5) + ((b & mask) >> 10);
*                 }
*             }
*
*/


#define N 200

// array of pixel's color intensity          (output)
int p[N];

unsigned int n;
unsigned int rd;
unsigned int r;                     
unsigned int gd; 
unsigned int g; 
unsigned int bd; 
int b;

main() {
  /*void gouraud(unsigned int n, unsigned int rd, unsigned int r, 
                          unsigned int gd, unsigned int g, unsigned int bd, 
                          int b, int p[])
  */
  unsigned int    mask = 0xF800F800, i;
  for (i = 0; i < N; i++) {
    r += rd;
    g += gd;
    b += bd;
    p[i] = (r & mask) + ((g & mask) >> 5) + ((b & mask) >> 10);
  }
}


