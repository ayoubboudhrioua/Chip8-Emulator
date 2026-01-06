#include <stdio.h>
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

uint32_t themes[][2] = {
    {0xFFFFFFFF, 0x000000FF}, // Classic B&W
    {0x00FF00FF, 0x000000FF}, // Matrix Green
    {0xFF00FFFF, 0x100020FF}, // Neon / Vaporwave
    {0xF6D6BDFF, 0x332c50FF}  // Gameboy
};


//SDL Container object
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_AudioSpec want , have;
    SDL_AudioDeviceID dev;
} sdl_t;

//Emulator states
typedef enum{
    QUIT,
    RUNNING,
    PAUSED,
} emulator_state_t;

// CHIP-8 extensions
typedef enum{
    CHIP8,
    SUPERCHIP,
    XOCHIP8,
}extension_t;



//Emulator configuration object
typedef struct {
    uint32_t window_width;                 //SDL window width
    uint32_t window_height;               //SDL window height
    uint32_t fg_color;                   //Foreground color 
    uint32_t bg_color;                  //Background color 
    uint32_t scale_factor;             // Amount to scale a CHIP8 pixel by e.g. 20x will be a 20x larger window
    bool pixel_outlines;              //Draw pixel outline
    uint32_t insts_per_second;       // CHIP8 CPU Clock rate or hz
    uint32_t square_wave_freq;       // Frequency of square wave sound e.g . 440hz for middle A
    uint32_t audio_sample_rate;      //(càd le nombre des echantillons)
    uint16_t volume;                 // How loud is the device
    extension_t current_extension;   // Current quirk/extension suport CHIP8 vs SUPERCHIP
    bool debug;
    bool show_metrics;
} config_t;



//CHIP8 Instruction format
typedef struct{
    uint16_t opcode; 
    uint16_t NNN;   //12 bit address/constant
    uint8_t NN;     //8 bit constant
    uint8_t N;      //4 bit constant
    uint8_t X;      //4 bit register identifier
    uint8_t Y;      //4 bit register identifier
}instruction_t;


// CHIP8 Machine object
typedef struct{
    emulator_state_t state;
    uint8_t ram[4096];
    bool display[64*32];   // Emulate Original CHIP8
    uint32_t pixel_color[64*32]; // CHIP8 pixel colors to draw
    uint16_t stack[12];  // Subroutine stack
    uint16_t *stack_ptr;
    uint8_t V[16];       // Data registers V0-VF
    uint16_t I;           // Index register
    uint16_t PC;         // Program Counter
    uint8_t delay_timer; // Decrements at 60hz when >0
    uint8_t sound_timer; // Decrements at 60hz and plays tone when >0 
    bool keypad[16];     // Hexadecimal keypad 0x0-0xF
    const char *rom_name;     // Currently running ROM
    instruction_t inst;      // Currently executing instruction
    bool draw;           // update the screen yes/no
    uint64_t total_instructions;
    double current_fps;
    uint32_t instructions_per_frame;
}chip8_t;

static const uint8_t simple_font[10][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
};

void draw_char(SDL_Renderer *renderer, char c, int x, int y, uint32_t color) {
    if (c < '0' || c > '9') return;
    
    uint8_t r = (color >> 24) & 0xFF;
    uint8_t g = (color >> 16) & 0xFF;
    uint8_t b = (color >> 8) & 0xFF;
    uint8_t a = (color) & 0xFF;
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    
    int digit = c - '0';
    for (int col = 0; col < 5; col++) {
        uint8_t column_data = simple_font[digit][col];
        for (int row = 0; row < 8; row++) {
            if (column_data & (1 << row)) {
                SDL_Rect pixel = {x + col * 2, y + row * 2, 2, 2};
                SDL_RenderFillRect(renderer, &pixel);
            }
        }
    }
}

void draw_text(SDL_Renderer *renderer, const char *text, int x, int y, uint32_t color) {
    int offset = 0;
    for (int i = 0; text[i] != '\0'; i++) {
        if (text[i] >= '0' && text[i] <= '9') {
            draw_char(renderer, text[i], x + offset, y, color);
            offset += 12; // 5 pixels + spacing
        } else if (text[i] == ' ') {
            offset += 8;
        } else if (text[i] == '.') {
            uint8_t r = (color >> 24) & 0xFF;
            uint8_t g = (color >> 16) & 0xFF;
            uint8_t b = (color >> 8) & 0xFF;
            uint8_t a = (color) & 0xFF;
            SDL_SetRenderDrawColor(renderer, r, g, b, a);
            SDL_Rect dot = {x + offset, y + 12, 2, 2};
            SDL_RenderFillRect(renderer, &dot);
            offset += 6;
        }
    }
}


// SDL audio callback

void audio_callback(void *userdata,uint8_t *stream,int len){
    config_t * config = (config_t *)userdata;

    int16_t *audio_data = (int16_t *)stream;
    static uint32_t running_sample_index = 0;
    const int32_t square_wave_period = config->audio_sample_rate / config->square_wave_freq;
    const int32_t half_square_wave_period = square_wave_period / 2;

    // We are filling out 2 bytes at a time (int16_t) len is in bytes;
    // so we divise by 2
    for (int i = 0; i < len / 2; i++){
        audio_data [i] = ((running_sample_index++ / half_square_wave_period) % 2) ? config->volume : -config->volume;
    }
}



// Forward declarations
void final_cleanup(const sdl_t sdl);


// Initialize SDL
bool init_sdl(sdl_t *sdl ,  config_t *config){
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) !=0) {
        SDL_Log("Could not initialize SDL: %s", SDL_GetError());
        return false;

    }

    sdl->window = SDL_CreateWindow("CHIP8 Emulator",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,config->window_width*config->scale_factor,config->window_height*config->scale_factor,0);
    if (!sdl->window){
        SDL_Log("Could not create window %s", SDL_GetError());
        return false;
    }

    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);
    if (!sdl->renderer){
        SDL_Log("Could not create SDL renderer %s\n", SDL_GetError());
        return false;
    }
    //  Init Audio:
    sdl ->want = (SDL_AudioSpec){
        .freq = 44100,       // 44100hz "CD" Quality
        .format = AUDIO_S16LSB, //Signed 16- bit little endian
        .channels = 1,     // mono-channel
        .samples = 512,
        .callback = audio_callback,
        .userdata = config,     //userdata passed to audio callback
    };

    sdl->dev = SDL_OpenAudioDevice(NULL,0, &sdl->want,&sdl->have ,0);
    if (sdl->dev == 0){
        SDL_Log("Could not ge an audio device %s\n",SDL_GetError());
        return false;
    }
    if ((sdl->want.channels != sdl->have.channels) || (sdl->want.format != sdl->have.format)){
        SDL_Log("Could not get desired Audio Spec\n");
        return false;
    }

    return true; //Success
}

bool set_config_from_args(config_t *config, const int argc,  char **argv){
    *config =(config_t){
        .window_width =64, //CHIP8 original x resolution
        .window_height =32, //CHIP8 original y resolution
        .fg_color = 0xFFFFFFFF, //White
        .bg_color = 0x000000FF, //Black
        .scale_factor = 20,     //Default resolution will be 1280x640
        .pixel_outlines = true, //Draw pixel outline by default
        .insts_per_second = 700, //Number of instructions to emulate in 1 second (clock rate of CPU)
        .square_wave_freq = 440, //440 hz for middle A
        .audio_sample_rate = 44100, // CD Quality
        .volume = 3000,          // Max Volume
        .current_extension = CHIP8, // Set default
        .debug = false,    // defalut debug to false
        .show_metrics = false,
    };

    for(int i=1; i<argc; i++){
        (void)argv[i];
        if (strncmp(argv[i],"--scale-factor", strlen("--scale-factor")) == 0){
            if (i+1 < argc){
                i++; 
                config->scale_factor =(uint32_t) strtol(argv[i],NULL,10);
            }
            
        }
        else if (strncmp(argv[i],"--debug",strlen("--debug")) == 0){
            config->debug = true;
        }
        else if (strncmp(argv[i],"--metrics",strlen("--metrics")) == 0){
            config->show_metrics = true;
        }

    }
    return true; //success

}
//Initialize CHIP8 machine
bool init_chip8(chip8_t *chip8,const config_t config ,const char rom_name[]){
    const uint32_t entry_point = 0x200; // CHIP8 ROMs will be loaded to 0x200
    const uint8_t font [] ={
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0 ->11110000 
        0x20, 0x60, 0x20, 0x20, 0x70, // 1   10010000
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2   10010000
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3   10010000
        0x90, 0x90, 0xF0, 0x10, 0x10, // 4   11110000
        0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5 
        0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
        0xF0, 0x10, 0x20, 0x40, 0x40, // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90, // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
        0xF0, 0x80, 0x80, 0x80, 0xF0, // C
        0xE0, 0x90, 0x90, 0x90, 0xE0, // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
        0xF0, 0x80, 0xF0, 0x80, 0x80  // F
    };

    // Initialize entire chipo8 machine
    memset(chip8,0,sizeof(chip8_t));

    //Load Font
    memcpy(&chip8->ram[0],font, sizeof(font));

    // Open ROM file
    FILE *rom = fopen(rom_name,"rb");
    if (!rom) {
        SDL_Log("Rom file %s is invalid or does not exist\n",rom_name);
        return false;
    }
    // Get check rom size
    fseek(rom,0,SEEK_END);
    const size_t rom_size = ftell(rom);
    const size_t max_size = sizeof(chip8->ram) - entry_point;
    rewind(rom);
    if (rom_size > max_size ){
        SDL_Log("ROM file %s is too big! ROM size: %zu , Max size Allowed: %zu\n",rom_name,rom_size,max_size);
        return false;

    }
    // Load ROM
    if (fread(&chip8->ram[entry_point],rom_size, 1,rom ) != 1){
        SDL_Log("Could not read ROM file %s into CHIP8 memory\n",rom_name);
        return false;
        
    }



    fclose(rom);

    // Set chip8 machine defaults
    chip8->state = RUNNING; //Default machine state to on/running
    chip8->PC = entry_point; //Start program counter at ROM entry point
    chip8->rom_name = rom_name;
    chip8->stack_ptr = &chip8->stack[0];
    memset(&chip8->pixel_color[0], config.bg_color ,sizeof chip8->pixel_color); // Init pixels to bg color

    return true; //success
}

//Final cleanup of SDL resources
void final_cleanup(const sdl_t sdl){
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_CloseAudioDevice(sdl.dev);
    SDL_Quit();//Shutdown SDL subsystems

}
// Clear screen / SDL Window to background color
void clear_screen(const sdl_t sdl ,const config_t config){
    const uint8_t r = (config.bg_color >> 24) & 0xFF;
    const uint8_t g = (config.bg_color >> 16) & 0xFF;
    const uint8_t b = (config.bg_color >> 8) & 0xFF;
    const uint8_t a = (config.bg_color >> 0) & 0xFF;

    SDL_SetRenderDrawColor(sdl.renderer,r,g,b,a);
    SDL_RenderClear(sdl.renderer);

}

void update_screen(const sdl_t sdl, const config_t config, chip8_t *chip8) {
    SDL_Rect rect = {0, 0, config.scale_factor, config.scale_factor};

    // 1. Extract Background Color components (R, G, B, A)
    uint8_t bg_r = (config.bg_color >> 24) & 0xFF;
    uint8_t bg_g = (config.bg_color >> 16) & 0xFF;
    uint8_t bg_b = (config.bg_color >> 8) & 0xFF;
    uint8_t bg_a = (config.bg_color) & 0xFF;

    // 2. Clear the entire screen to the Background color
    // This wipes away any "shadows" or trails from the previous frame
    SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
    SDL_RenderClear(sdl.renderer);

    // 3. Prepare to draw Foreground pixels
    uint8_t fg_r = (config.fg_color >> 24) & 0xFF;
    uint8_t fg_g = (config.fg_color >> 16) & 0xFF;
    uint8_t fg_b = (config.fg_color >> 8) & 0xFF;
    uint8_t fg_a = (config.fg_color) & 0xFF;
    SDL_SetRenderDrawColor(sdl.renderer, fg_r, fg_g, fg_b, fg_a);

    // 4. Loop through all 2048 pixels
    for (uint32_t i = 0; i < 2048; i++) {
        // If the pixel is ON (1), draw a rectangle
        if (chip8->display[i]) {
            // Calculate X and Y coordinates
            uint32_t x = (i % 64) * config.scale_factor;
            uint32_t y = (i / 64) * config.scale_factor;

            rect.x = x;
            rect.y = y;
            
            // Draw the pixel
            SDL_RenderFillRect(sdl.renderer, &rect);
        }
    }

    // 5. Present the changes
    SDL_RenderPresent(sdl.renderer);
}

//Handle user input
//CHIP8 keypad  QWERTY
//123C          1234
//456D          QWER
//789E          asdf
//A0BF          zxcv
void handle_input(chip8_t *chip8,config_t *config,sdl_t *sdl){
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                //Exit window;End program
                chip8->state = QUIT; //will exit main emulator loop
                break;
            case SDL_DROPFILE: {
                char* dropped_file = event.drop.file;
                // Reset the machine with the new ROM
                if (init_chip8(chip8,*config,dropped_file)){
                    SDL_Log("Loaded ROM: %s",dropped_file);
                    // update window title
                    SDL_SetWindowTitle(sdl->window,dropped_file);
                }
                SDL_free(dropped_file);  // SDL require to free the filename string
                break;
            }
            case SDL_KEYDOWN:
                switch (event.key.keysym.scancode){
                    case SDL_SCANCODE_ESCAPE:
                        //ESCAPE key; Exit the window & End program
                        chip8->state = QUIT;
                        break;
                    case SDL_SCANCODE_SPACE:
                        //space bar
                        if (chip8->state == RUNNING){
                            chip8->state = PAUSED; //PAUSE
                            puts("=== PAUSED ===");
                        } else{
                            chip8->state = RUNNING; //RESUME
                        }
                        break;
                    

                    // Map qwerty keys to chip8 keypad
                    case SDL_SCANCODE_1: chip8->keypad[0x1] = true; break;
                    case SDL_SCANCODE_2: chip8->keypad[0x2] = true; break;
                    case SDL_SCANCODE_3: chip8->keypad[0x3] = true; break;
                    case SDL_SCANCODE_4: chip8->keypad[0xC] = true; break;

                    case SDL_SCANCODE_Q: chip8->keypad[0x4] = true; break;
                    case SDL_SCANCODE_W: chip8->keypad[0x5] = true; break;
                    case SDL_SCANCODE_E: chip8->keypad[0x6] = true; break;
                    case SDL_SCANCODE_R: chip8->keypad[0xD] = true; break;

                    case SDL_SCANCODE_A: chip8->keypad[0x7] = true; break;
                    case SDL_SCANCODE_S: chip8->keypad[0x8] = true; break;
                    case SDL_SCANCODE_D: chip8->keypad[0x9] = true; break;
                    case SDL_SCANCODE_F: chip8->keypad[0xE] = true; break;

                    case SDL_SCANCODE_Z: chip8->keypad[0xA] = true; break;
                    case SDL_SCANCODE_X: chip8->keypad[0x0] = true; break;
                    case SDL_SCANCODE_C: chip8->keypad[0xB] = true; break;
                    case SDL_SCANCODE_V: chip8->keypad[0xF] = true; break;

                    default: break;

                }
                switch (event.key.keysym.sym){
                    case SDLK_EQUALS:
                        // '=': Reset CHIP8 MACHINE
                        init_chip8(chip8,*config,chip8->rom_name);
                        break;

                        case SDLK_o:
                        // 'o' decrease volume
                        if (config->volume > 0){
                            config->volume -= 500;
                        }
                        break;
                    case SDLK_p:
                        // 'p' increase volume
                        if (config->volume < INT16_MAX){
                            config->volume += 500;
                        }
                        break;
                    case SDLK_m:{
                        static int theme_idx = 0;
                        theme_idx = (theme_idx + 1) % 4;  // cycle from 0 to 3
                        config->fg_color = themes[theme_idx][0];
                        config->bg_color = themes[theme_idx][1];
                        break;
                    }
                    case SDLK_i:
                        // 'i' toggle performance metrics (Info)
                        config->show_metrics = !config->show_metrics;
                        break;
                    default: break;
                }
                break;

            case SDL_KEYUP:
                switch (event.key.keysym.scancode) {
                    // Map qwerty keys to chip8 keypad
                    case SDL_SCANCODE_1: chip8->keypad[0x1] = false; break;
                    case SDL_SCANCODE_2: chip8->keypad[0x2] = false; break;
                    case SDL_SCANCODE_3: chip8->keypad[0x3] = false; break;
                    case SDL_SCANCODE_4: chip8->keypad[0xC] = false; break;

                    case SDL_SCANCODE_Q: chip8->keypad[0x4] = false; break;
                    case SDL_SCANCODE_W: chip8->keypad[0x5] = false; break;
                    case SDL_SCANCODE_E: chip8->keypad[0x6] = false; break;
                    case SDL_SCANCODE_R: chip8->keypad[0xD] = false; break;

                    case SDL_SCANCODE_A: chip8->keypad[0x7] = false; break;
                    case SDL_SCANCODE_S: chip8->keypad[0x8] = false; break;
                    case SDL_SCANCODE_D: chip8->keypad[0x9] = false; break;
                    case SDL_SCANCODE_F: chip8->keypad[0xE] = false; break;

                    case SDL_SCANCODE_Z: chip8->keypad[0xA] = false; break;
                    case SDL_SCANCODE_X: chip8->keypad[0x0] = false; break;
                    case SDL_SCANCODE_C: chip8->keypad[0xB] = false; break;
                    case SDL_SCANCODE_V: chip8->keypad[0xF] = false; break;

                }
                break;
            default:
                break;
        }

    }

}

void print_debug_info(chip8_t *chip8){
    printf("Address: 0x%04X, Opcode: 0x%04X Desc: ",chip8->PC-2,chip8->inst.opcode);
    switch ((chip8->inst.opcode >> 12) & 0x0F){
    case 0x0:
        if (chip8->inst.NN == 0xE0){
            // 0x00E0: Clear the screen
            printf("Clear screen\n");
        } else if (chip8->inst.NN == 0xEE){
            //0x00EE: Return from subroutine
            // Set PC to last address on subroutine stack ("pop" it off the stack)
            // so that the next opcode will be gotten from that address
            printf("Return from subroutine to address 0x%04X\n",*(chip8->stack_ptr -1));
        }else{
            printf("Unimplemented Opcode.\n");
        }
        break;
    case 0x01:
        // 0x1NNN : Jump to address NNN
        printf("Jump to address NNN (0x%04X)\n",chip8->inst.NNN);
        break;

    case 0x02:
        //0x2NNN : Call subroutine at NNN
        // Store current subroutine to return to on subroutine stack("push" it on the stack)
        // and set program counter to subroutine address so that the 
        // next opcode is gotten from there
        printf("Call subroutine at NNN (0x%04X)\n",chip8->inst.NNN);
        break;
    case 0x03:
        // 0x3XNN: check if VX == NN if so skip the next instruction
        printf("ChecK if V%X (0x%02X) == NN (0x%02X), skip next instruction if true\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.NN);
        break;
    case 0x04:
        // 0x4XNN: check if VX != NN if so skip the next instruction
        printf("ChecK if V%X (0x%02X) != NN (0x%02X), skip next instruction if true\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.NN);
        break;
    case 0x05:
        // 0x5XY0: check if VX == VY if so skip the next instruction
        printf("Check if V%X (0x%02X) == V%X (0x%02X), skip next instruction if true\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.Y,chip8->V[chip8->inst.Y]);
        break;
    case 0x06:
        //0x6XNN: Set register VX to NN
        printf("Set register V%X = NN (0x%02X)\n",chip8->inst.X,chip8->inst.NN);
        break;
    case 0x07:
        // 0x7XNN: Set register VX +=NN
        printf("Set register V%X (0x%02X) += NN (0x%02X). Result: 0x%02X\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.NN,chip8->V[chip8->inst.X] + chip8->inst.NN);
        break;
    case 0x08:
        switch(chip8->inst.N){
            case 0:
                // 0x8XY0: Set register VX = VY
                printf("Set register V%X = V%X (0x%02X)\n",chip8->inst.X,chip8->inst.Y,chip8->V[chip8->inst.Y]);
                break;
            case 1:
                // 0x8XY1:set register VX |= VY
                printf("Set register V%X (0x%02X) |= V%X (0x%02X) ; Result : 0x%02X\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.Y,chip8->V[chip8->inst.Y],chip8->V[chip8->inst.X] | chip8->V[chip8->inst.Y]);
                break;
            case 2:
                // 0x8XY2:set register VX &= VY
                printf("Set register V%X (0x%02X) &= V%X (0x%02X) ; Result : 0x%02X\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.Y,chip8->V[chip8->inst.Y],chip8->V[chip8->inst.X] & chip8->V[chip8->inst.Y]);
                break;
            case 3:
                // 0x8XY3:set register VX ^= VY
                printf("Set register V%X (0x%02X) ^= V%X (0x%02X) ; Result : 0x%02X\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.Y,chip8->V[chip8->inst.Y],chip8->V[chip8->inst.X] ^ chip8->V[chip8->inst.Y]);
                break;
            case 4:
                // 0x8XY4:set register VX += VY,set VF to 1 if carry
                printf("Set register V%X (0x%02X) += V%X (0x%02X), VF = 1 if carry  ; Result : 0x%02X , VF = %X\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.Y,chip8->V[chip8->inst.Y],chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y],((uint16_t)(chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y]) > 255));
                break;
            case 5:
                // 0x8XY5: set register VX -= VY set VF to 1 if there is not a borrow (result is positive/0)
                printf("Set register V%X (0x%02X) -= V%X (0x%02X), VF = 1 if no borrow; Result : 0x%02X , VF = %X\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.Y,chip8->V[chip8->inst.Y],chip8->V[chip8->inst.X] - chip8->V[chip8->inst.Y],(chip8->V[chip8->inst.Y] <= chip8->V[chip8->inst.X]));
                break;
            case 6:
                // 0x8XY6: Set register VX >>= 1 , store shifted off bit in VF
                printf("Set register V%X (0x%02X) >>= 1, VF = shifted off bit (%X); Result: 0x%02X\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->V[chip8->inst.X] & 1 , chip8->V[chip8->inst.X] >> 1);
                break;
            case 7:
                // 0x8XY7: Set register VX = VY - VX, set VF to 1 if there is not a borrow (result is positive/0)
                printf("Set register V%X = V%X (0x%02X) - V%X (0x%02X), VF = 1 if no borrow ; Result : 0x%02X , VF = %X\n",chip8->inst.X,chip8->inst.Y,chip8->V[chip8->inst.Y],chip8->inst.X,chip8->V[chip8->inst.X],chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X],(chip8->V[chip8->inst.X] <= chip8->V[chip8->inst.Y]));
                break;
            case 0xE:
                // 0x8XYE: set register VX <<= 1,store shifted off bit in VF
                printf("Set register V%X (0x%02X) <<= 1, VF = shifted off bit (%X); Result: 0x%02X\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->V[chip8->inst.X] >> 7 , chip8->V[chip8->inst.X] << 1);
                break;


            default:
                // wrong/unimplemented opcode
                break;
        }
        break;
    case 0x09:
        // 0x9XY0 : Check if VX != VY; Skip next instruction if so
        printf("Check if V%X (0x%02X) != V%X (0x%02X), skip next instruction if true\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.Y,chip8->V[chip8->inst.Y]);
        break;

    case 0x0A: 
        //0xANNN: Set index register I to NNN
        printf("Set I to NNN (0x%04X)\n",chip8->inst.NNN);
        break;
    case 0x0B:
        // 0xBNNN: Jump to V0 + NNN
        printf("Set PC to V0 (0x%02X) + NNN (0x%04X)\n",chip8->V[0],chip8->inst.NNN,chip8->V[0] + chip8->inst.NNN);
        break;
    case 0x0C:
        //0xCNNN: sets register VX = rand() % 256 & NN (bitwise AND)
        printf("Set V%X = rand() %% 256 & NN (0x%02X)\n",chip8->inst.X ,chip8->inst.NN);
        break;

    case 0x0D:
        // 0xDXYN : Draw N-height sprite at coords X,Y; Read from memory location I;
        // Screen pixels are XOR'd with sprite bits,
        // VF (carry flag) is set if any screen pixels are set off ; this is useful 
        // collision detection or other reasons
        printf("Draw N (%u) height sprite at cords V%X (0x%02X) , V%X (0x%02X) from memory location I (0x%04X). Set VF = 1 if any pixels are turned off.\n",chip8->inst.N,chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.Y,chip8->V[chip8->inst.Y],chip8->I);
        break;
    case 0x0E:
        if (chip8->inst.NN == 0x9E){
            //0xEX9E: Skip next instruction if key in VX is pressed
            printf("skip next instruction if key in V%X (0x%02X) is pressed; Keypad value: %d\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->keypad[chip8->V[chip8->inst.X]]);
            
        } else if (chip8->inst.NN == 0xA1){
            //0xEX9E: skip next instruction if key is not pressed
            printf("skip next instruction if key in V%X (0x%02X) is not pressed; Keypad value: %d\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->keypad[chip8->V[chip8->inst.X]]);
        }
        break;
    case 0x0F:
        switch (chip8->inst.NN) {
            case 0x0A:
                // 0xFX0A: VX = get_key(): Await until a keypress, and store in VX
                printf("Await until akey is pressed; Store key in V%X\n",chip8->inst.X);
                break;
            case 0x1E:
                // 0xFx1E: I += VX Add VX to register I For non-Amiga CHIP8, does not affect VF
                printf("I (0x%04X) += V%X (0x%02X); Result (I) : 0x%04X\n",chip8->I,chip8->inst.X,chip8->V[chip8->inst.X],chip8->I + chip8->V[chip8->inst.X]);
                break;
            case 0x07:
                // 0xFX07: VX = delay timer
                printf("Set V%X = delay_timer value (0x%02X)\n",chip8->inst.X,chip8->delay_timer);
                break;
            case 0x15:
                // 0xFX15: delay_timer = VX
                printf("Set delay_timer value = V%X (0x%02X)\n",chip8->inst.X,chip8->V[chip8->inst.X]);
                break;
            case 0x18:
                //0xFX18: sound_timer = VX
                printf("Set sound_timer value = V%X (0x%02X)\n",chip8->inst.X,chip8->V[chip8->inst.X]);
                break;
            case 0x29:
                //0xFX29: Set register I to sprite location in memory in VX[0x0 -0xF]
                printf("Set I to sprite location in memory for character in V%X (0x%02X) . Result(VX*5) = (0x%02X)\n",chip8->inst.X,chip8->V[chip8->inst.X], chip8->V[chip8->inst.X] *5);
                break;
            case 0x33:
                //0xFX33: Store BCD representation of VX at memory offset from I;
                // I = hundred's place, I+1 = ten's place, I+2 = one's place
                printf("Store BCD representation of V%X (0x%02X) at memory from I (0x%04X)\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->I);
                break;
            case 0x55:
                //0xFX55: Register dump V0-VX inclusive to memory offset from I;
                // SCHIP8 does not increment I , CHIP8 does increment I
                printf("Register dump V0-V%X (0x%02X) inclusive at memory from I (0x%04X)\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->I);
                break;
            case 0x65:
                //0xFX65: Register load V0-VX inclusive from memory offset from I;
                // SCHIP8 does not increment I , CHIP8 does increment I
                printf("Register load V0-V%X (0x%02X) inclusive at memory from I (0x%04X)\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->I);
                break;
            
            default:
                break;
            

        }
        


    default:
        printf("Unimplemented Opcode.");
        break; // Unimplemented or invalid opcode
}
}



//Emulate 1 CHIP8 instruction
void emulate_instructions(chip8_t *chip8, const config_t config) {
    bool carry;      // save carry falg VF value for some intsructions
    chip8->total_instructions++;
    // Get next opcode from RAM
    chip8->inst.opcode = (chip8->ram[chip8->PC] <<8) | chip8->ram[chip8->PC+1];
    chip8->PC +=2; //Pre increment program counter for next opcode

    // Fill out current instruction format
    chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;
    chip8->inst.NN = chip8->inst.opcode & 0x0FF;
    chip8->inst.N = chip8->inst.opcode & 0x0F;
    chip8->inst.X = (chip8->inst.opcode >> 8) & 0x0F;
    chip8->inst.Y = (chip8->inst.opcode >> 4) & 0x0F;

    if (config.debug){
        print_debug_info(chip8);
    }



    //Emulate opcode
    switch ((chip8->inst.opcode >> 12) & 0x0F){
        case 0x00:
            if (chip8->inst.NN == 0xE0){
                // 0x00E0: Clear the screen
                memset(&chip8->display[0],false, sizeof chip8->display);
                chip8->draw = true; //will update screen on next 60hz tick
            } else if (chip8->inst.NN == 0xEE) {
                //0x00EE: Return from subroutine
                // Set PC to last address on subroutine stack ("pop" it off the stack)
                // so that the next opcode will be gotten from that address
                chip8->PC = *--chip8->stack_ptr;
            } else {
                //Unimplemented invalid opcode maybe 0xNNN for calling machine code routine for RCA1802

            }
            break;
        case 0x01:
            // 0x01NNN : Jump to address NNN
            chip8->PC = chip8->inst.NNN ;// Set program counter so that next opcode is from NNN
            break;
            

        case 0x02:
            //0x2NNN : Call subroutine at NNN
            // Store current subroutine to return to on subroutine stack("push" it on the stack)
            // and set program counter to subroutine address so that the 
            // next opcode is gotten from there
            *chip8->stack_ptr++ = chip8->PC; 
            chip8->PC = chip8->inst.NNN;
            break;
        case 0x03:
            // 0x3XNN: Check if VX == NN if so skip the next instruction
            if (chip8->V[chip8->inst.X] == chip8->inst.NN){
                chip8->PC += 2;   //skip next opcode/instruction
            }    
            break;
        case 0x04:
            // 0x4XNN: check if VX != NN if so, skip the next instruction
            if (chip8->V[chip8->inst.X] != chip8->inst.NN){
                chip8->PC += 2;   //skip next opcode/instruction
            }    
            break;
        case 0x05:
            // 0x5XY0: check if VX == VY if so skip the next instruction
            if (chip8->inst.N !=0) break; //wrong opcode
            if (chip8->V[chip8->inst.X] == chip8->V[chip8->inst.Y]){
                chip8->PC += 2;
            }
            break;
        case 0x06:
            // 0x6XNN : Set register VX to NN
            chip8->V[chip8->inst.X] = chip8->inst.NN;
            break;
        case 0x07:
            // 0x7XNN: Set register VX +=NN
            chip8->V[chip8->inst.X] += chip8->inst.NN;
            break;
        case 0x08:
            switch(chip8->inst.N){
                case 0:
                     // 0x8XY0: Set register VX = VY
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y];
                    break;
                case 1:
                    // 0x8XY1:set register VX |= VY
                    chip8->V[chip8->inst.X] |= chip8->V[chip8->inst.Y];
                    if (config.current_extension == CHIP8){
                        chip8->V[0xF] = 0;  // Reset VF to 0
                    }
                    break;
                case 2:
                    // 0x8XY2:set register VX &= VY
                    chip8->V[chip8->inst.X] &= chip8->V[chip8->inst.Y];
                    if (config.current_extension == CHIP8){
                        chip8->V[0xF] = 0;  // Reset VF to 0
                    }
                    break;
                case 3:
                    // 0x8XY3:set register VX ^= VY
                    chip8->V[chip8->inst.X] ^= chip8->V[chip8->inst.Y];
                    if (config.current_extension == CHIP8){
                        chip8->V[0xF] = 0;  // Reset VF to 0
                    }
                    break;
                case 4: {
                    // 0x8XY4:set register VX += VY,set VF to 1 if carry
                    carry = ((uint16_t)(chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y]) > 255);
                    chip8->V[chip8->inst.X] += chip8->V[chip8->inst.Y];
                    chip8->V[0xF] = carry;
                    break;
                }
                case 5:{
                    // 0x8XY5: set register VX -= VY set VF to 1 if there is not a borrow (result is positive/0)
                    carry = (chip8->V[chip8->inst.Y] <= chip8->V[chip8->inst.X]);
                    chip8->V[chip8->inst.X] -= chip8->V[chip8->inst.Y];
                    chip8->V[0xF] = carry;
                    break;
                }
                case 6:{
                    // 0x8XY6: Set register VX >>= 1, store shifted off bit in VF
                    if (config.current_extension == CHIP8) {
                        carry = chip8->V[chip8->inst.Y] & 1;    // Use VY
                        chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] >> 1; // Set VX = VY result
                    } else {
                        carry = chip8->V[chip8->inst.X] & 1;    // Use VX
                        chip8->V[chip8->inst.X] >>= 1;          // Use VX
                    }
                    chip8->V[0xF] = carry;
                    break;
                }
                case 7:{
                    // 0x8XY7: Set register VX = VY - VX, set VF to 1 if there is not a borrow (result is positive/0)
                    carry = (chip8->V[chip8->inst.Y] >= chip8->V[chip8->inst.X]);
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X];
                    chip8->V[0xF] = carry;
                    break;
                }
                case 0xE:{
                    // 0x8XYE: set register VX <<= 1,store shifted off bit in VF
                    if (config.current_extension == CHIP8) {
                        carry = (chip8->V[chip8->inst.Y] & 0x80) >> 7;    // Use VY
                        chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] << 1;   //Set VX = VY result
                    } else {
                        carry = (chip8->V[chip8->inst.X] & 0x80) >> 7;    // VX
                        chip8->V[chip8->inst.X] <<= 1;                   // Use VX
                    }
                    chip8->V[0xF] = carry;
                    break;
                }
                default:
                    // wrong/unimplemented opcode
                    break;
            }
            break;
        case 0x09:
            // 0x9XY0: Check if VX != VY;skip next instruction if so
            if (chip8->V[chip8->inst.X] != chip8->V[chip8->inst.Y]){
                chip8->PC += 2;
            }
            break;
        case 0x0A:
            // 0xANNN Set index register I to NNN
            chip8->I = chip8->inst.NNN;
            break;
        case 0x0B:
            // 0xBNNN: Jump to V0 + NNN
            chip8->PC =chip8->V[0] + chip8->inst.NNN;
            break;
        case 0x0C:
            //0xCNNN: sets register VX = rand() % 256 & NN (bitwise AND)
            chip8->V[chip8->inst.X] = (rand() % 256) & chip8->inst.NN;
            break;
        case 0x0D:{
            // 0xDXYN : Draw N-height sprite at coords X,Y; Read from memory location I;
            // Screen pixels are XOR'd with sprite bits,
            // VF (carry flag) is set if any screen pixels are set off ; this is useful 
            // collision detection or other reasons
            uint8_t X_coord = chip8->V[chip8->inst.X] % config.window_width;
            uint8_t Y_coord = chip8->V[chip8->inst.Y] % config.window_height;
            const uint8_t orig_X = X_coord;
            chip8->V[0xF] = 0; //Initialize carry flag to 0
            for (uint8_t i = 0; i < chip8->inst.N; i++){
                //Get next byte/row of sprite data
                const uint8_t sprite_data = chip8->ram[chip8->I + i];
                X_coord = orig_X; // Reset X for next row to draw
                for (int8_t j = 7; j >=0 ; j--){
                    //IF sprite pixel/bit is on and display pixel is on, set carry flag
                    bool *pixel = &chip8->display[Y_coord * config.window_width + X_coord];
                    const bool sprite_bit = (sprite_data & (1 << j));
                    if (sprite_bit && *pixel){
                        chip8->V[0xF] = 1;
                    }

                    // XOR display pixel with sprite pixel/bit to set it on or off
                    *pixel ^= sprite_bit;
                    
                    // Stop drawing if hit right edge of screen
                    if (++X_coord >= config.window_width) break;

                }
                // Stop drawing entire sprite if hit bottom edge of screen
                if (++Y_coord >= config.window_height) break;

            }
            chip8->draw = true; // will update screen on 60hz tick
            break;
        }
        case 0x0E:
            if (chip8->inst.NN == 0x9E){
                // 0xEX9E: Skip next instruction if key in VX is pressed
                if (chip8->keypad[chip8->V[chip8->inst.X]]){
                    chip8->PC += 2;
                }

            }else if (chip8->inst.NN == 0xA1){
                // 0xEX9E: Skip next instruction if key in VX is not pressed
                if (!chip8->keypad[chip8->V[chip8->inst.X]]){
                    chip8->PC += 2;
                }

            }
            break;
        case 0x0F:
            switch (chip8->inst.NN) {
                case 0x0A:{
                    // 0xFX0A: VX = get_key(); await until a keypress, and store in VX
                    static bool any_key_pressed = false;
                    static uint8_t key = 0xFF;
                    for (uint8_t i =0; key == 0xFF && i< sizeof chip8->keypad; i++){
                        if (chip8->keypad[i]){
                            key = i;        // Save pressed key to check until it is released
                            chip8->V[chip8->inst.X] = i;  // i = key (offset into keypad array)
                            any_key_pressed = true;
                            break;
                        }
                    }
                    // if no key has been pressed yet keep getting the current opcode & running this instruction
                    if (!any_key_pressed){
                        chip8->PC -= 2;
                    } else {
                        // A key has been pressed, also wait until it is released to set the key in VX
                        if (chip8->keypad[key]){  // Busy loop CHIP8 emulation until key is released
                            chip8->PC -= 2;
                        } else {
                            chip8->V[chip8->inst.X] = key;    //VX = key
                            key = 0XFF;    // Reset key to not found
                            any_key_pressed = false;  // Reset to nothin   g pressed yet
                        }
                    }
                    break;
                }
                case 0x1E:
                    // 0xFX1E I += VX; add VX to register I for non-Amiga CHIP8, does not affect VF
                    chip8->I += chip8->V[chip8->inst.X];
                    break;
                case 0x07:
                    // 0xFX07: VX = delay timer
                    chip8->V[chip8->inst.X] = chip8->delay_timer;
                    break;
                case 0x15:
                    // 0xFX15: delay_timer = VX
                    chip8->delay_timer = chip8->V[chip8->inst.X];
                    break;
                case 0x18:
                    //0xFX18: sound_timer = VX
                    chip8->sound_timer = chip8->V[chip8->inst.X];
                    break;
                case 0x29:
                    //0xFX29: Set register I to sprite location in memory in VX[0x0 -0xF]
                    chip8->I = chip8->V[chip8->inst.X] * 5;
                    break;
                case 0x33:{
                    // 0xFX33: Store BCD representations of VX at memory offset from I;
                    //  I = hundred's place, I+1 = ten's place, I+2 = one's place
                    uint8_t bcd = chip8->V[chip8->inst.X]; // e.g .12[3]
                    chip8->ram[chip8->I+2] = bcd % 10;
                    bcd /= 10;
                    chip8->ram[chip8->I+1] = bcd % 10;
                    bcd /= 10;
                    chip8->ram[chip8->I] = bcd;
                    break;
                }
                case 0x55:
                    //0xFX55: Register dump V0-VX inclusive to memory offset from I;
                    // SCHIP8 does not increment I , CHIP8 does increment I
                    // Note: could make this a config flag to use SCHIP8 or CHIP8 logic for I
                    for (uint8_t i = 0; i <= chip8->inst.X; i++)  {
                        if (config.current_extension == CHIP8) 
                            chip8->ram[chip8->I++] = chip8->V[i]; // Increment I each time
                        else
                            chip8->ram[chip8->I + i] = chip8->V[i]; 
                    }
                    break;
                case 0x65:
                    // 0xFX65: Register load V0-VX inclusive from memory offset from I;
                    //   SCHIP does not increment I, CHIP8 does increment I
                    for (uint8_t i = 0; i <= chip8->inst.X; i++)  {
                        if (config.current_extension == CHIP8) 
                            chip8->V[i] = chip8->ram[chip8->I++]; // Increment I each time
                        else
                            chip8->V[i] = chip8->ram[chip8->I + i]; 
                    }
                    break;

                default:
                    break;
            }
            break;

        default:
            break; // Unimplemented or invalid opcode
    }
}
//Update chip8 delay and sound timers every 60hz
void update_timers(const sdl_t sdl,chip8_t *chip8){
    if (chip8->delay_timer > 0) chip8->delay_timer--;
    if (chip8->sound_timer > 0) {
        chip8->sound_timer--;
        SDL_PauseAudioDevice(sdl.dev,0); // play Sound

    } else {
        SDL_PauseAudioDevice(sdl.dev,1); //Play sound

    }
}
// Display performance metrics on screen
void display_metrics(const sdl_t sdl, const chip8_t *chip8, const config_t config) {
    if (!config.show_metrics) return;
    
    // Draw semi-transparent background for metrics
    SDL_Rect bg_rect = {5, 5, 250, 70};
    SDL_SetRenderDrawBlendMode(sdl.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl.renderer, 0, 0, 0, 180);
    SDL_RenderFillRect(sdl.renderer, &bg_rect);
    
    char buffer[32];
    uint32_t text_color = 0x00FF00FF; // Green text
    
    // Display FPS
    snprintf(buffer, sizeof(buffer), "FPS %d", (int)chip8->current_fps);
    draw_text(sdl.renderer, buffer, 10, 10, text_color);
    
    // Display instructions per frame
    snprintf(buffer, sizeof(buffer), "IPF %d", chip8->instructions_per_frame);
    draw_text(sdl.renderer, buffer, 10, 30, text_color);
    
    // Display total instructions (in thousands)
    snprintf(buffer, sizeof(buffer), "INS %luk", (unsigned long)(chip8->total_instructions / 1000));
    draw_text(sdl.renderer, buffer, 10, 50, text_color);
    
    SDL_SetRenderDrawBlendMode(sdl.renderer, SDL_BLENDMODE_NONE);
}




//main function
int main (int argc , char **argv){
    //Default Usage message for args
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <rom_name> [--scale-factor N] [--debug] [--metrics]\n",argv[0]);
        exit(EXIT_FAILURE);
    }
    //2. Print Controls (Run this EVERY time, even if the ROM is valid)
    printf("==========================================\n");
    printf("Controls:\n");
    printf("  I      : Toggle performance metrics\n");
    printf("  M      : Change color theme\n");
    printf("  SPACE  : Pause/Resume\n");
    printf("  ESC    : Quit\n");
    printf("  O/P    : Increase/Decrease Volume\n");
    printf("==========================================\n");

    //Initialize emulator configuration/options
    config_t config = {0};
    if (!set_config_from_args(&config, argc,argv)) exit(EXIT_FAILURE); 

    //Initialize SDL
    sdl_t sdl = {0};
    if(!init_sdl(&sdl,&config)) exit(EXIT_FAILURE);

    //Initialize CHIP8 machine
    chip8_t chip8 = {0};
    const char *rom_name = argv[1];
    if (!init_chip8(&chip8,config ,rom_name)) exit(EXIT_FAILURE);

    //Initial screen clear
    clear_screen(sdl,config);
    // Seed random number generator
    srand(time(NULL));
    
    //Main Emulator loop
    while (chip8.state != QUIT){
        handle_input(&chip8,&config,&sdl);

        if (chip8.state == PAUSED) continue;
        // Get_time() before running intsruction;
        const uint64_t start_frame_time = SDL_GetPerformanceCounter();
        // emulate CHIP8 Instructions for this emulator "frame" (60hz)
        uint32_t inst_count = config.insts_per_second / 60;
        chip8.instructions_per_frame = inst_count;
        for (uint32_t i = 0;i < inst_count;i++){
            emulate_instructions(&chip8,config);
        }
        // Get_time elapsed after running instruction;
        const uint64_t end_frame_time = SDL_GetPerformanceCounter(); 
        // Delay for approximately 60hz/60fps (16.67ms) or actual time elapsed
        const double time_elapsed = (double)((end_frame_time - start_frame_time) * 1000) / SDL_GetPerformanceFrequency();
        chip8.current_fps = (time_elapsed > 0) ? 1000.0 / time_elapsed : 60.0;
        if (chip8.current_fps > 60.0) chip8.current_fps = 60.0; // Cap display at 60
        // SDL_Delay(16-actual time elapsed);
        SDL_Delay(16.67f > time_elapsed ? 16.67f - time_elapsed : 0);
        //Update window with changes every 60hz
        if (chip8.draw){
            update_screen(sdl,config,&chip8);
            chip8.draw = false;
        }
        if (config.show_metrics){
            display_metrics(sdl,&chip8,config);
            SDL_RenderPresent(sdl.renderer);
        }
        //Update delay & sound timers
        update_timers(sdl,&chip8);
    }
    //Final cleanup and exit
    final_cleanup(sdl);
    return EXIT_SUCCESS;
}