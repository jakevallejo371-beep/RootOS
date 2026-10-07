#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

void set_function_idt(int index, void (*fn)(void), uint8_t fn_attributes);

//#define NULL 0

#define VGA_width 80
#define VGA_height 25

#define CURSOR_START_REGISTER 0x0A

#define VGA_CTRL_PORT 0x3D4
#define VGA_DATA_PORT 0x3D5
#define VGA_CURSOR_HIGH 0x0E
#define VGA_CURSOR_LOW  0x0F

#define keyboard_data_port 0x60
#define keyboard_status_port 0x64

#define int_gate 0x8E

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define ICW1_INIT    0x10
#define ICW1_ICW4    0x01
#define ICW4_8086    0x01

#define ring_buffer_size 6


#define hex_black 0x0
#define hex_blue 0x1
#define hex_green 0x2
#define hex_cyan 0x3
#define hex_red 0x4
#define hex_mangenta 0x5
#define hex_brown 0x6
#define hex_light_grey 0x7
#define hex_dark_grey 0x8
#define hex_light_blue 0x9
#define hex_light_green 0xA
#define hex_light_cyan 0xB
#define hex_light_red 0xC
#define hex_light_mangenta 0xD
#define hex_yellow 0xE
#define hex_white 0xF

#define history_size 64
#define command_size 100



#define trigger_interrupt(vector) __asm__ volatile ("int %0" : :"N"(vector) )


struct color {
    int back;
    int fore;
    int error;
    int warning;
    int keyword;
};

struct color theme = { hex_black, hex_white, hex_red, hex_yellow, hex_green };

int calcVGA_color(struct color theme) {
    return (theme.back << 4) | (theme.fore & 0x0F);
}

//enum colorThemes currentTheme = RETROORANGE;

extern void hang();            //asm
extern void recovery();       //asm
extern void keyboard_isr();  //asm

//----------------------------------------------------[Interrupt descriptor table]

//IDT data type
typedef struct {
    uint16_t isr_low;
    uint16_t kernel_cs;
    uint8_t  reserved;
    uint8_t  attributes;
    uint16_t isr_high;
} __attribute__((packed)) idt_entry_t;

//create idt table
static idt_entry_t idt[256];

//idtr data type
typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idtr_t;

//declare idtr
idtr_t idtr = {
    .limit = 2047,
    .base  = (uint32_t)idt
};

/* 

[IDT layout]

 - 0-31 -> Intel exceptions
 - 32-255 -> Software interrupts

*/

uint16_t updateCSvar(void);     // Fix order

void set_function_idt(int index, void (*fn)(void), uint8_t fn_attributes) {
    uint32_t addr = (uint32_t)fn;

    idt[index].isr_low  = addr & 0xFFFF;              // bottom 16 bits
    idt[index].kernel_cs = updateCSvar();            // almost always 0x08
    idt[index].reserved = 0;                        // always 0
    idt[index].attributes = fn_attributes;         // amlost always 0x8E(Present, Ring 0, Interrupt Gate) for hardware interupts and 0xEE(Present, Ring 3, Interrupt Gate) for user-mode system calls
    idt[index].isr_high = (addr >> 16) & 0xFFFF;  // top 16 bits
}

//------------------------------------------------------------[asmebly functions]

uint16_t updateCSvar(void) {
    uint16_t cs;

    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    return cs;
}

void fn_lidtr(idtr_t idtr) {
    __asm__ volatile ("lidt %0" : : "m" (idtr));
}

extern void test_isr(void);     //In boot.asm

// Declare the 32 assembly wrappers for Intel exceptions
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);

void set_intelException(void) {
    void (*exception_functions[32])(void) = {
        isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
        isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
        isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
        isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    };

    for (int i=0; i < 32; i++)
    {
        set_function_idt(i, exception_functions[i], int_gate);
    }

    fn_lidtr(idtr);
}

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(unsigned short port, unsigned short val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline char inb(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline unsigned short inw(unsigned short port) {
    unsigned short ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void sti(void) {
    __asm__ volatile ("sti" : : );
}

static inline void hlt(void) {
    __asm__ volatile ("hlt" : : );
}

//---------------------------------------------------------------------[PIC remap]

//delay
static inline void io_wait(void) {
    outb(0x80, 0);
}

void pic_remap(int offset1, int offset2) {

    // 2. ICW1: Start initialization sequence on both PICs
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    // 3. ICW2: Set vector offsets
    outb(PIC1_DATA, offset1); // Master vector offset (e.g., 0x20)
    io_wait();
    outb(PIC2_DATA, offset2); // Slave vector offset (e.g., 0x28)
    io_wait();

    // 4. ICW3: Configure cascade relationship
    outb(PIC1_DATA, 4); // Tell Master PIC that Slave PIC is at IRQ2 (0000 0100b)
    io_wait();
    outb(PIC2_DATA, 2); // Tell Slave PIC its cascade identity (2)
    io_wait();

    // 5. ICW4: Set 8086 mode
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    outb(PIC1_DATA, 0xFD);
    outb(PIC2_DATA, 0xFF);
}

//-------------------------------------------------------------------------------[VGA buffer functions]

volatile unsigned short* vga_buffer = (unsigned short*)0xB8000;

unsigned int cursor_x = 0;
unsigned int cursor_y = 0;

void enableHardwareCursor(uint8_t cursor_start, uint8_t cursor_end) {
    outb(VGA_CTRL_PORT, CURSOR_START_REGISTER);
    // Fetch current register value, clear top bits, set scanlines, and ensure Bit 5 is 0 (0 = Enable, 1 = Disable)
    outb(VGA_DATA_PORT, (inb(VGA_DATA_PORT) & 0xC0) | cursor_start);
    
    // 0x0B is the Cursor End Register
    outb(VGA_CTRL_PORT, 0x0B);
    outb(VGA_DATA_PORT, (inb(VGA_DATA_PORT) & 0xE0) | cursor_end);
}

void disableBlink(void) {
    inb(0x3DA);                 // reset the attribute controller flip-flop
    outb(0x3C0, 0x30);          // select register 0x10 (Mode Control), keep display enabled
    unsigned char mode = (unsigned char)inb(0x3C1);
    mode &= ~0x08;              // bit 3 = blink enable
    outb(0x3C0, mode);
}

void updateHardwareCursor(int x, int y) {
    unsigned short pos = y * VGA_width + x;
    
    outb(VGA_CTRL_PORT, VGA_CURSOR_LOW);
    outb(VGA_DATA_PORT, (unsigned short)(pos & 0xFF));
    
    outb(VGA_CTRL_PORT, VGA_CURSOR_HIGH);
    outb(VGA_DATA_PORT, (unsigned short)((pos >> 8) & 0xFF));
}

void clearScreen(const char* args) {
    (void)args;

    for (int i=0; i < 2000; i++)
    {
        vga_buffer[i] = (unsigned short)' ' | ((unsigned short)calcVGA_color(theme) << 8);
    }

    cursor_x = 0;
    cursor_y = 0;
}


void scroll(void) {
    for (int i = 0; i < (VGA_height - 1) * VGA_width; i++) {
        vga_buffer[i] = vga_buffer[i + VGA_width];
    }

    int last_row_start = (VGA_height - 1) * VGA_width;
    for (int i = 0; i < VGA_width; i++) {
        vga_buffer[last_row_start + i] = (unsigned short)' ' | ((unsigned short)calcVGA_color(theme) << 8);
    }

    cursor_y = VGA_height - 1;

    updateHardwareCursor(cursor_x, cursor_y);
}

void printCord(const char* string, int cur_x, int cur_y) {
    int x = cur_x;
    int y = cur_y;

    for (int i=0; string[i] != '\0'; i++)
    {
        if (y >= VGA_height)
        {
            scroll();
            y = VGA_height - 1;
        }

        cursor_x = x;
        cursor_y = y;

        if (string[i] == '\n')
        {
            x = cur_x;
            y++;
            continue;
        }

        int index = (y * VGA_width) + (x++);
        vga_buffer[index] = (unsigned short)string[i] | ((unsigned short)calcVGA_color(theme) << 8);

        if (x >= VGA_width)
        {
            x = 0;
            y++;
        }

        updateHardwareCursor(x, y);
    }
}

void print(const char* string) {
    int x = cursor_x;
    int y = cursor_y;

    for (int i=0; string[i] != '\0'; i++)
    {
        if (x >= VGA_width)
        {
            x = 0;
            y++;
        }

        if (y >= VGA_height)
        {
            scroll();
            y = VGA_height - 1;
        }

        if (string[i] == '\n') {
            x = 0;
            y++;
            continue;
        }

        int index = (y * VGA_width) + x;
        vga_buffer[index] = (unsigned short)string[i] | ((unsigned short)calcVGA_color(theme) << 8);
        x++;
    }

    cursor_x = x;
    cursor_y = y;

    updateHardwareCursor(x, y);
}

void printError(const char* string) {
    int x = cursor_x;
    int y = cursor_y;

    for (int i=0; string[i] != '\0'; i++)
    {
        if (x >= VGA_width)
        {
            x = 0;
            y++;
        }

        if (y >= VGA_height)
        {
            scroll();
            y = VGA_height - 1;
        }

        if (string[i] == '\n') {
            x = 0;
            y++;
            continue;
        }

        int index = (y * VGA_width) + x;
        vga_buffer[index] = (unsigned short)string[i] | ((unsigned short)((theme.back << 4) | (theme.error & 0x0F)) << 8);
        x++;
    }

    cursor_x = x;
    cursor_y = y;

    updateHardwareCursor(x, y);
}

void printWarning(const char* string) {
    int x = cursor_x;
    int y = cursor_y;

    for (int i=0; string[i] != '\0'; i++)
    {
        if (x >= VGA_width)
        {
            x = 0;
            y++;
        }

        if (y >= VGA_height)
        {
            scroll();
            y = VGA_height - 1;
        }

        if (string[i] == '\n') {
            x = 0;
            y++;
            continue;
        }

        int index = (y * VGA_width) + x;
        vga_buffer[index] = (unsigned short)string[i] | ((unsigned short)((theme.back << 4) | (theme.warning & 0x0F)) << 8);
        x++;
    }

    cursor_x = x;
    cursor_y = y;

    updateHardwareCursor(x, y);
}

void printChar(const char c) {
    int x = cursor_x;
    int y = cursor_y;
    if (x >= VGA_width)
    {
        x = 0;
        y++;
    }

    if (y >= VGA_height)
    {
        scroll();
        y = VGA_height - 1;
    }

    if (c == '\n') {
        x = 0;
        y++;
    }

    int index = (y * VGA_width) + x;
    vga_buffer[index] = (unsigned short)c | ((unsigned short)calcVGA_color(theme) << 8);
    x++;

    cursor_x = x;
    cursor_y = y;

    updateHardwareCursor(x, y);
}

void printCharCommand(const char c) {
    int x = cursor_x;
    int y = cursor_y;
    if (x >= VGA_width)
    {
        x = 0;
        y++;
    }

    if (y >= VGA_height)
    {
        scroll();
        y = VGA_height - 1;
    }

    if (c == '\n') {
        x = 0;
        y++;
    }

    int index = (y * VGA_width) + x;
    vga_buffer[index] = (unsigned short)c | ((unsigned short)((theme.back << 4) | (theme.keyword & 0x0F)) << 8);
    x++;

    cursor_x = x;
    cursor_y = y;

    updateHardwareCursor(x, y);
}

void printCharCord(const char c, int cur_x, int cur_y) {
    int x = cur_x;
    int y = cur_y;
    if (x >= VGA_width)
    {
        x = 0;
        y++;
    }

    if (y >= VGA_height)
    {
        scroll();
        y = VGA_height - 1;
    }

    if (c == '\n') {
        x = 0;
        y++;
    }   

    int index = (y * VGA_width) + x;
    vga_buffer[index] = (unsigned short)c | ((unsigned short)calcVGA_color(theme) << 8);

    cursor_x = x;
    cursor_y = y;

    updateHardwareCursor(x, y);
}

char* int_to_string(int num) {
    static char buffer[32];
    int i = 0;
    bool neg = false;

    if (num == 0) {
        buffer[i] = '0';
        i++;
        buffer[i] = '\0';
        return buffer;
    }

    if (num < 0) {
        neg = true;
        num = -num;
    }

    while (num > 0) {
        buffer[i++] = (char)('0' + (num % 10));
        num /= 10;
    }

    buffer[i] = '\0';

    for (int j = 0; j < i / 2; j++) {
        char temp = buffer[j];
        buffer[j] = buffer[i - 1 - j];
        buffer[i - 1 - j] = temp;
    }
    
    if (neg)
    {
        buffer[i++] = '-';
    }

    buffer[i] = '\0';

    return buffer;
}

int string_to_int(const char *str) {
    int result = 0;
    int sign = 1;
    int i = 0;

    // 1. Skip leading whitespace
    while (str[i] == ' ' || str[i] == '\t' || str[i] == '\n' || str[i] == '\r') {
        i++;
    }

    // 2. Check for optional sign
    if (str[i] == '-') {
        sign = -1;
        i++;
    } else if (str[i] == '+') {
        i++;
    }

    // 3. Process numeric characters
    while (str[i] >= '0' && str[i] <= '9') {
        // Multiply by 10 to shift left, then add the new digit value
        result = (result * 10) + (str[i] - '0');
        i++;
    }

    // 4. Return final value with sign applied
    return result * sign;
}


bool stringCompare(char* string1, char* string2) {
    int i=0;
    while (string1[i] && string2[i])
    {
        if (string1[i] != string2[i])
        {
            return false;
        }
        i++;
    }
    return string1[i] == '\0' && string2[i] == '\0';
}

void stringCopy(char* destination, const char* source)
{
    int i = 0;

    while (source[i] != '\0' && i < command_size - 1)
    {
        destination[i] = source[i];
        i++;
    }

    destination[i] = '\0';
}

//-------------------------------------------------------------------------------------------[acpi power off]

struct acpi_rsdp {
    char     signature[8];
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;
} __attribute__((packed));

struct acpi_header {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_fadt {
    struct acpi_header header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t  reserved;
    uint8_t  pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  s4bios_req;
    uint8_t  pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
} __attribute__((packed));

static bool memEqual(const void* a, const char* b, int n) {
    const char* p = (const char*)a;
    for (int i = 0; i < n; i++) {
        if (p[i] != b[i]) return false;
    }
    return true;
}

static struct acpi_rsdp* findRSDP(void) {
    uint32_t ebda = ((uint32_t)*(volatile uint16_t*)0x40E) << 4;
    uint32_t start[2] = { ebda, 0xE0000 };
    uint32_t end[2]   = { ebda + 1024, 0x100000 };

    for (int r = 0; r < 2; r++) {
        if (start[r] == 0) continue;
        for (uint32_t addr = start[r]; addr < end[r]; addr += 16) {
            if (memEqual((void*)addr, "RSD PTR ", 8)) {
                uint8_t sum = 0;
                for (int i = 0; i < 20; i++) sum += ((uint8_t*)addr)[i];
                if (sum == 0) return (struct acpi_rsdp*)addr;
            }
        }
    }
    return NULL;
}

static struct acpi_fadt* findFADT(struct acpi_rsdp* rsdp) {
    struct acpi_header* rsdt = (struct acpi_header*)rsdp->rsdt_address;
    if (!memEqual(rsdt->signature, "RSDT", 4)) return NULL;

    uint32_t count = (rsdt->length - sizeof(struct acpi_header)) / 4;
    uint32_t* entries = (uint32_t*)((uint8_t*)rsdt + sizeof(struct acpi_header));

    for (uint32_t i = 0; i < count; i++) {
        struct acpi_header* h = (struct acpi_header*)entries[i];
        if (memEqual(h->signature, "FACP", 4)) return (struct acpi_fadt*)h;
    }
    return NULL;
}

static void acpiPowerOff(void) {
    struct acpi_rsdp* rsdp = findRSDP();
    struct acpi_fadt* fadt = rsdp ? findFADT(rsdp) : NULL;

    if (fadt != NULL && fadt->dsdt != 0 && fadt->pm1a_cnt_blk != 0) {
        uint16_t pm1a = (uint16_t)fadt->pm1a_cnt_blk;
        uint16_t pm1b = (uint16_t)fadt->pm1b_cnt_blk;

        // Switch the chipset into ACPI mode if the firmware hasn't already (SCI_EN = bit 0)
        if ((inw(pm1a) & 1) == 0 && fadt->smi_cmd != 0 && fadt->acpi_enable != 0) {
            outb((uint16_t)fadt->smi_cmd, fadt->acpi_enable);
            for (int i = 0; i < 3000000 && (inw(pm1a) & 1) == 0; i++) io_wait();
        }

        // Find the _S5_ object in the DSDT to get the sleep type values
        struct acpi_header* dsdt = (struct acpi_header*)fadt->dsdt;
        uint8_t* p   = (uint8_t*)dsdt + sizeof(struct acpi_header);
        uint8_t* end = (uint8_t*)dsdt + dsdt->length;

        for (; p + 8 < end; p++) {
            if (memEqual(p, "_S5_", 4) && p[4] == 0x12 &&
                (p[-1] == 0x08 || (p[-2] == 0x08 && p[-1] == '\\')))
            {
                uint8_t* s = p + 5;
                s += ((*s & 0xC0) >> 6) + 2;       // skip PkgLength and NumElements
                if (*s == 0x0A) s++;               // skip byte prefix
                uint16_t typA = (uint16_t)(*s << 10);
                s++;
                if (*s == 0x0A) s++;
                uint16_t typB = (uint16_t)(*s << 10);

                outw(pm1a, typA | (1 << 13));      // SLP_EN = bit 13
                if (pm1b != 0) outw(pm1b, typB | (1 << 13));
                break;
            }
        }
    }

    // Fallbacks for emulators
    outw(0x604,  0x2000);   // QEMU
    outw(0xB004, 0x2000);   // older QEMU / Bochs
    outw(0x4004, 0x3400);   // VirtualBox
}


//----------------------------------------------------------------------------------------[Keyboard driver]

volatile char ring_buffer[6];

volatile int head_elemt = 0;
volatile int tail_elemt = 0;


void keyboard_handler() {
    ring_buffer[head_elemt] = inb(0x60);
    head_elemt = (head_elemt + 1) % ring_buffer_size;
    
    //send the end of interrput
    outb(PIC1_COMMAND, 0x20);
};

char scancodeToASCII(unsigned char scancode) {
    static bool shift = false;

    //lower case
    const char map[128] = {
        0,  0, '1','2','3','4','5','6','7','8','9','0','-','=',
        '\b','\t','q','w','e','r','t','y','u','i','o','p','[',']',
        '\n',0,'a','s','d','f','g','h','j','k','l',';','\'','`',
        0,'\\','z','x','c','v','b','n','m',',','.','/',0,0,0,
        ' ',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    };

    //upper case
    const char shift_map[128] = {
        0,  0, '!','@','#','$','%','^','&','*','(',')','_','+',
        '\b','\t','Q','W','E','R','T','Y','U','I','O','P','{','}',
        '\n',0,'A','S','D','F','G','H','J','K','L',':','"','~',
        0,'|','Z','X','C','V','B','N','M','<','>','?',0,0,0,
        ' ',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    };

    //if shift was released
    if (scancode & 0x80)
    {
        unsigned char releaseScancode = scancode & 0x7F;
        if (releaseScancode == 0x2A || releaseScancode == 0x36)
        {
            shift = false;
        }
        return 0;
    }

    //if shift pressed
    if (scancode == 0x2A || scancode == 0x36) {
        shift = true;
        return 0; 
    }

    //cap lock released
    if (scancode & 0x80)
    {
        unsigned char releaseScancode = scancode & 0x7F;
        if (releaseScancode == 0x3A)
        {
            shift = false;  
        }
        return 0;
    }

    //cap lock pressed
    if (scancode == 0x3A)
    {
        shift = !shift;
        return 0;
    }

    return (shift) ? shift_map[scancode] : map[scancode];
}

char* readline(void) {
    static char keys[100];
    int i = 0;
    char c = '\0';
    while (1)
    {
        if (i == 99)
        {
            print("ERROR: key array limit reached");
            return keys;
        }

        while (head_elemt == tail_elemt)
            hlt();

        c = scancodeToASCII(ring_buffer[tail_elemt]);
        tail_elemt = (tail_elemt + 1) % ring_buffer_size;

        if (c <= 0) { continue; }

        if (c == '\n')
        {
            keys[i] = '\0';
            print("\n");
            return keys;
        }

        if (c == '\b' && i > 0)
        {
            i--;
            keys[i] = '\0';
            if  (cursor_x == 0)
            {
                cursor_x = VGA_width -1;
                cursor_y--;
            } else {
                cursor_x--;
            }
            printCharCord(' ', cursor_x, cursor_y);
        }
        else if (c == '\b' && i == 0) { continue;} 
        else {
            keys[i] = c;
            i++;
            printChar(c);
        }
    }
}

char* readCommand(void) {
    static char keys[100];
    int i = 0;
    char c = '\0';
    unsigned int typingCord = -1;
    while (1)
    {
        if (i == 99)
        {
            print("ERROR: key array limit reached");
            return keys;
        }

        while (head_elemt == tail_elemt)
            hlt();

        c = scancodeToASCII(ring_buffer[tail_elemt]);
        tail_elemt = (tail_elemt + 1) % ring_buffer_size;

        if (c <= 0) { continue; }

        if (c == '\n')
        {
            keys[i] = '\0';
            print("\n");
            return keys;
        }

        if (c == 0x20)
            typingCord = cursor_x;

        if (c == '\b' && i > 0)
        {
            i--;
            keys[i] = '\0';
            if  (cursor_x == 0)
            {
                cursor_x = VGA_width -1;
                cursor_y--;
                typingCord = cursor_x;;
            } else {
                cursor_x--;
            }
            printCharCord(' ', cursor_x, cursor_y);

        }
        else if (c == '\b' && i == 0) { continue;} 
        else {
            keys[i] = c;
            i++;
            (cursor_x <= typingCord) ? printCharCommand(c) : printChar(c);
        }
    }
}

//------------------------------------------------------------------------------------------------[shell]

typedef void (*cmd_func) (const char* args);

#define cmd_count (sizeof(cmd_table) / sizeof(cmd_table[0]))

typedef struct {
    const char* name;
    cmd_func fptr;
    bool args_flag;
    const char* outline;
} cmd_entry;


bool commands = true;


void shutdown(const char* args) {
    (void)args;

    printWarning("Are you sure you want to shutdown?\n");
    print("[y/n]: ");
    char* answer = readline();
    if (stringCompare(answer, "y") || stringCompare(answer, "Y"))
    {
        commands = false;
        printWarning("System shuting down...");
        __asm__ volatile ("cli");
        acpiPowerOff();
        printError("Error: failed to shutdown");
        return;
    }
    else if (stringCompare(answer, "n") || stringCompare(answer, "N"))
    {
        printWarning("canceling...\n");
        return;
    } else {
        printError("Invial responce\n");
        printWarning("canceling...\n");
    }
}

void printTitle(const char* args)
{
    (void)args;

    print(
    " _____________________________________\n"
    "   ____              _    ___  ____  \n"
    "  |  _ \\ ___   ___ _| |_ / _ \\/ ___| \n"
    "  | |_) / _ \\ / _ \\_   _| | | \\___ \\ \n"
    "  |  _ < (_) | (_) || |_| |_| |___) |\n"
    "  |_| \\_\\___/ \\___/  \\__|\\___/|____/ \n"
    " _____________________________________\n\n"
    );
}

void test_interrupt_handler_caller(const char* args) {
    int vector = string_to_int(args);
    printWarning("Calling interrupt vector: ");
    print(args);
    print("...\n");

    switch (vector)
    {
    case 48:
        trigger_interrupt(0x30);
        break;
    
    default:
        printError("Couldn't find interrupt ");
        printError(args);
        printError(" in IDT\n");
        break;
    }
}

void test_interrupt_handler_output(const char* args) {
    (void)args;

    print("interupt test succesful\n");
}

void help(const char* args);

void echo(const char* args) {
    print(args);
    print("\n");
}

void reboot(const char* args) {
    (void)args;

    printWarning("Are you sure you want to reboot?\n");
    print("[y/n]: ");
    char* answer = readline();
    if (stringCompare(answer, "y") || stringCompare(answer, "Y"))
    {
        printWarning("rebooting down...");
        outb(0x64, 0xFE); // Pulse CPU reset line
    }
    else if (stringCompare(answer, "n") || stringCompare(answer, "N"))
    {
        printWarning("canceling...\n");
        return;
    } else {
        printError("Invial responce\n");
        printWarning("canceling...\n");
    }
}

void VGA_color(const char* args) {
    if (args == NULL)
    {
        print("Themes: dark, light, retro_green, retro_orange, blue_retro, and ash\n");
        return;
    }

    else if (stringCompare((char*)args, "dark"))
    {
        theme.back    = hex_black;
        theme.fore    = hex_white;
        theme.error   = hex_red;
        theme.warning = hex_yellow;
        theme.keyword = hex_green;
        clearScreen(NULL);
        printTitle(NULL);
    }
    
    else if (stringCompare((char*)args, "light"))
    {
        theme.back    = hex_white;
        theme.fore    = hex_black;
        theme.error   = hex_red;
        theme.warning = hex_yellow;
        theme.keyword = hex_green;
        clearScreen(NULL);
        printTitle(NULL);

    }

    else if (stringCompare((char*)args, "retro_green"))
    {
        theme.back    = hex_black;
        theme.fore    = hex_green;
        theme.error   = hex_red;
        theme.warning = hex_light_blue;
        theme.keyword = hex_yellow;
        clearScreen(NULL);
        printTitle(NULL);
    }

    else if (stringCompare((char*)args, "retro_orange"))
    {
        theme.back    = hex_black;
        theme.fore    = hex_yellow;
        theme.error   = hex_red;
        theme.warning = hex_mangenta;
        theme.keyword = hex_white;
        clearScreen(NULL);
        printTitle(NULL);
    }

    else if (stringCompare((char*)args, "blue_retro"))
    {
        theme.back    = hex_blue;
        theme.fore    = hex_white;
        theme.error   = hex_red;
        theme.warning = hex_yellow;
        theme.keyword = hex_light_cyan;
        clearScreen(NULL);
        printTitle(NULL);
    }

    else if (stringCompare((char*)args, "ash"))
    {
        theme.back    = hex_light_grey;
        theme.fore    = hex_black;
        theme.error   = hex_red;
        theme.warning = hex_yellow;
        theme.keyword = hex_green;
        clearScreen(NULL);
        printTitle(NULL);
    }
    
    else {
        printError(args);
        printError(" is not a theme\n");
    }
}

void version(const char* args) {
    (void)args;

    print(
        "      RootOS Alpha\n"
        "+------------------------+\n"
        "Version:               1.1\n"
        "CPU:                32-bit\n"
        "Bootloader:   Multiboot v1\n"
        "Kernel type:          .bin\n"
        "Languages:    C and x86-32\n"
        "Total lines of code: 1,481\n"
    );
}

static int history_cursor = 0;
static char cmd_history[history_size][command_size];

void print_history(const char* args) {
    if (args == NULL)
    {
        int j = 1;
        for (int i=0; i < history_size; i++)
        {
            if (cmd_history[i][0] != '\0')
            {
                print(int_to_string(j));
                j++;
                print("] ");
                print(cmd_history[i]);
                print("\n");
            }
        }
    }
    else if (stringCompare((char*)args, "cursor"))
    {
        print("DEBUG history_cursor = ");
        print(int_to_string(history_cursor));
        print("\n");
    }
}

void status(const char* args) {
    (void)args;
    print(
        "Architecture:        i386\n"
        "PIC:                 remaped\n"
        "Interrupts:          enabled\n"
        "keyboard driver:     endbled\n"
        "VGA dimensions:      80x25\n"
    );
}

void print_credit(const char* args) {
    (void)args;

    print(
        "Dev name       | Worked on\n"
        "---------------|----------------------\n"
        "Jacob Vallejo  | kernel and bootloader\n"
    );
}


static const cmd_entry cmd_table[] = {
    {"help",           help,                          false, "Displays information"},
    {"shutdown",       shutdown,                      false, "Shuts down the operating system"},
    {"reboot",         reboot,                        false, "reboots the system"},
    {"clear",          clearScreen,                   false, "Clears the screen"},
    {"banner",         printTitle,                    false, "Displays the ASSII logo"},
    {"theme",          VGA_color,                     true,  "Changes background and foreground color to selected theme"},
    {"echo",           echo,                          true,  "prints the argument"},
    {"int",            test_interrupt_handler_caller, false, "Tells the CPU to do the interrpt in (integer)argument"},
    {"version",        version,                       false, "Prints OS info"},
    {"status",         status,                        false, "Prints kernels state"},
    {"credit",         print_credit,                  false, "Prints developers"},
    {"history",        print_history,                 true,  "Shows command history"}
};

int findSpace(char* str) {
    int i=0;
    while (str[i] != '\0')
    {
        if (str[i] == ' ')
        {  
            return i;
        }
        i++;
    }
    return -1;
}

void help(const char* args) {
    if (args == NULL)
    {
        for (size_t i=0; i < cmd_count; i++)
        {   
            print(cmd_table[i].name);
            print(" -> ");
            print(cmd_table[i].outline);
            print("\n");
        }
        return;
    }
    for (size_t i=0; i < cmd_count; i++)
    {
        if (stringCompare((char*)args, (char*)cmd_table[i].name))
        {
            print(cmd_table[i].outline);
            print("\n");
            return;
        }
    }
    printError("No command named ");
    printError(args);
    printError(" found\n");
}

void findCmd(char* input) {
    if (input == NULL)
    {
        printError("error, command was a NULL (y didn't u type anything?)");
        return;
    }

    int space_index = findSpace(input);
    char* args = NULL;

    if (space_index != -1)
    {
        input[space_index] = '\0';
        args = &input[space_index + 1];
    }

    for (size_t i=0; i < cmd_count; i++)
    {
        if (stringCompare(input, (char*)cmd_table[i].name))
        {
            cmd_table[i].fptr(args);
            return;
        }
    }

    printError("Command not found :(");
}

//--------------------------------------------------------------[terminal]

extern void terminal(void) {
    char* inputBuffer = NULL;
    while (commands)
    {
        print("\n>");
        inputBuffer = readCommand();
        //add command to history
        stringCopy(cmd_history[history_cursor], inputBuffer);
        findCmd(inputBuffer);
        history_cursor++;
    }
}

//--------------------------------------------------------------[exception_handler]

extern void exception_handler(int vector, int error_code) {
    printError("Runtime error!\n");
    print("Vector: ");
    printWarning(int_to_string(vector));
    print("\nerror code: ");
    printWarning(int_to_string(error_code));
    print("\n");

    if (vector == 8 || vector == 18 || vector == 10)
    {
        shutdown(NULL);
    }
}

//----------------------------------------------------------------------------------[Main function]

void main(void) {
    clearScreen(NULL);
    printTitle(NULL);
    disableBlink();
    enableHardwareCursor(12, 13);

    set_intelException();
    //add test to idt
    set_function_idt(0x30, test_isr, int_gate);
    pic_remap(0x20, 0x28);
    //add keyboard to idt
    set_function_idt(0x21, keyboard_isr, int_gate);

    fn_lidtr(idtr);
    sti();

    terminal();
}