#include <types.h>
#include <kernel.h>
#include <arch.h>
#include <lapic.h>
#include <msr.h>
#include <paging.h>
#include <segment.h>
#include <spinlock.h>
#include <in_out.h>
#include <cpuid.h>
#include <acpi.h>
#include <string.h>

void (*jmp_table_percpu[MAX_CPUS])() = {0};
struct bootloader_parm_block *boot_parm = (void *)0x10000;

u8 cpu_bitmap[128] = {0};
u64 irq_table[256];
u64 exception_table[32];

void set_kernel_segment()
{
	u8 *percpu_base = (u8 *)PHYS2VIRT(get_percpu_area_base());
	struct gdtr gdtr_percpu;
	struct segment_desc *gdt_base = (void *)(percpu_base + 0);

	set_segment_descriptor(gdt_base,
		SELECTOR_NULL_INDEX,
		0,
		0,
		0,
		0
	);
	set_segment_descriptor(gdt_base,
		SELECTOR_KERNEL_CODE_INDEX,
		0,
		0,
		CS_NC | DPL0,
		CS_L
	);
	set_segment_descriptor(gdt_base,
		SELECTOR_KERNEL_DATA_INDEX,
		0,
		0,
		DS_S_W | DPL0,
		0
	);
	set_segment_descriptor(gdt_base,
		SELECTOR_USER_CODE_INDEX,
		0,
		0,
		CS_NC | DPL3,
		CS_L
	);
	set_segment_descriptor(gdt_base,
		SELECTOR_USER_DATA_INDEX,
		0,
		0,
		DS_S_W | DPL3,
		0
	);

	gdtr_percpu.base = (u64)gdt_base;
	gdtr_percpu.limit = 256 * sizeof(struct segment_desc) - 1;
	
	lgdt(&gdtr_percpu);

	load_cs(SELECTOR_KERNEL_CODE);
	load_ds(SELECTOR_KERNEL_DATA);
	load_es(SELECTOR_KERNEL_DATA);
	load_fs(SELECTOR_KERNEL_DATA);
	load_gs(SELECTOR_KERNEL_DATA);
	load_ss(SELECTOR_KERNEL_DATA);
}

void set_intr_desc()
{
	/* defined in isr.asm */
	extern u64 exception_table[];
	extern u64 irq_table[];

	u8 *percpu_base = (u8 *)PHYS2VIRT(get_percpu_area_base());
	struct idtr idtr_percpu;
	struct gate_desc *idt_base = (void *)(percpu_base + 0x1000);
	int i;

	for (i = 0; i < 0x20; i++) {
		set_gate_descriptor(idt_base,
			i,
			SELECTOR_KERNEL_CODE,
			(u64)exception_table[i],
			0,
			TRAP_GATE | DPL0
		);
	}
	for (i = 0x20; i < 0x100; i++) {
		if (i == 0x80)
			continue;
		set_gate_descriptor(idt_base,
			i,
			SELECTOR_KERNEL_CODE,
			irq_table[i],
			0,
			INT_GATE | DPL0
		);
	}
	set_gate_descriptor(idt_base,
		0x80,
		SELECTOR_KERNEL_CODE,
		irq_table[0x80],
		0,
		TRAP_GATE | DPL3
	);

	idtr_percpu.base = (u64)idt_base;
	idtr_percpu.limit = 256 * sizeof(struct gate_desc) - 1;
	
	lidt(&idtr_percpu);
}

void set_tss_desc()
{
	u8 *percpu_base = (u8 *)PHYS2VIRT(get_percpu_area_base());
	struct segment_desc *gdt_base = (void *)(percpu_base + 0);
	struct tss_desc *tss = (void *)(percpu_base + 0x2000);

	memset(tss, 0, sizeof(*tss));
	tss->rsp0 = (u64)(percpu_base + PERCPU_AREA_SIZE);
	set_segment_descriptor(gdt_base,
		SELECTOR_TSS_INDEX,
		(u64)tss,
		sizeof(*tss) - 1,
		S_TSS | DPL0,
		0
	);
	
	ltr(SELECTOR_TSS);
}

void map_kernel_memory()
{

}

void wakeup_all_processors()
{
	extern u64 ap_startup_vector;
	extern u64 ap_startup_end;
	u64 ap_load_addr = 0x20000;
	u8 *dest = (u8 *)PHYS2VIRT(ap_load_addr);
	u8 *src = (u8 *)&ap_startup_vector;
	int i;

	/* copy ap startup code to AP_START_ADDR */
	for (i = 0; i < (u64)&ap_startup_end - (u64)&ap_startup_vector; i++)
		dest[i] = src[i];

	struct acpi_madt *madt_ptr = acpi_get_desc("APIC");
	if (madt_ptr == NULL) {
		//mp_init_all(ap_load_addr);
		return;
	}
	
	struct processor_lapic_structure *lapic_ptr;
	u8 *ptr = (u8 *)madt_ptr + sizeof(*madt_ptr);

	while (ptr[1] != 0 && (u64)ptr < (u64)madt_ptr + madt_ptr->header.length) {
		switch(ptr[0]) {
			case PROCESSOR_LOCAL_APIC:
				lapic_ptr = (struct processor_lapic_structure *)ptr;
				if (lapic_ptr->apic_id != 0) {
					//printk("waking up CPU:APIC ID = %d\n", lapic_ptr->apic_id);
					cpu_bitmap[lapic_ptr->apic_id] = 0;
					mp_init_single(lapic_ptr->apic_id, ap_load_addr);
					while(!cpu_bitmap[lapic_ptr->apic_id]);
				}
				break;
		}
		ptr += ptr[1];
	}
}

void ap_work()
{
	
}

void check_point()
{
}

void arch_init()
{
	u8 buffer[64] = {0};
	bool bsp;
	u8 cpu_id = 0;
	cpuid(0x00000001, 0x00000000, (u32 *)&buffer[0]);
	cpu_id = buffer[7];

	cpuid(0x80000002, 0x00000000, (u32 *)&buffer[0]);
	cpuid(0x80000003, 0x00000000, (u32 *)&buffer[16]);
	cpuid(0x80000004, 0x00000000, (u32 *)&buffer[32]);
	cpu_bitmap[cpu_id] = 1;
	printk("[CPU %02d] %s\n", cpu_id, buffer);

	bsp = is_bsp();
	if (bsp) {
		map_kernel_memory();
		wakeup_all_processors();
	} else {
		ap_work();
	}
	set_kernel_segment();
	set_intr_desc();
	set_tss_desc();

#if CONFIG_AMP
	jmp_table_percpu[cpu_id]();
#endif
	check_point();

	asm("hlt");
}