// Open file for PNG library
void * myOpen(const char *filename, int32_t *size) {
  myfile = SD.open(filename);
  *size = myfile.size();
  return &myfile;
}

// Close file for PNG library
void myClose(void *handle) {
  if (myfile) myfile.close();
}

// Read from file for PNG library
int32_t myRead(PNGFILE *handle, uint8_t *buffer, int32_t length) {
  if (!myfile) return 0;
  return myfile.read(buffer, length);
}

// Seek in file for PNG library
int32_t mySeek(PNGFILE *handle, int32_t position) {
  if (!myfile) return 0;
  return myfile.seek(position);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Draw PNG Scanline
 * Callback function to draw a line of pixels from the PNG decoder to the display.
 */
void PNGDraw(PNGDRAW *pDraw) {
  uint16_t usPixels[320];
  png.getLineAsRGB565(pDraw, usPixels, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff); // Convert line to RGB565
  gfx1->draw16bitRGBBitmap(0, pDraw->y + 0, usPixels, pDraw->iWidth, 1);      // Draw to display buffer
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Print Next PNG
 * Loads and displays the next slice image for the current layer.
 */
void print_next_png(){
  FileName = foldersel_long;
  FileName += "/";
  current_layer ++;
  int i = current_layer;
  // If layer height > 0.06, skip images
  // handling different slicing resolutions 
  if(Layer_Height > 0.06)
    i = current_layer * 2 - 1; 
  FileName += i;
  FileName += ".png";
  char NameChar[110];
  FileName.toCharArray(NameChar, 110);
  int rc;
  rc = png.open((const char *)NameChar, myOpen, myClose, myRead, mySeek, PNGDraw);
  if (rc == PNG_SUCCESS) {
    rc = png.decode(NULL, 0);
    png.close();
  }
  //entry.close();
  delay(50);  
}
