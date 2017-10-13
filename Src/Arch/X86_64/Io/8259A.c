#include "8259A.h"


RETURN_STATUS Init8259A()
{
	Out8(I8259A_MASTER_ICW1_PORT, ICW1_FIXED);
	Out8(I8259A_SLAVE_ICW1_PORT, ICW1_FIXED);

	Out8(I8259A_MASTER_ICW2_PORT, 0x20);
	Out8(I8259A_SLAVE_ICW2_PORT, 0x28);

	Out8(I8259A_MASTER_ICW3_PORT, ICW3_MASTER_FIXED);
	Out8(I8259A_SLAVE_ICW3_PORT, ICW3_SLAVE_FIXED);

	Out8(I8259A_MASTER_ICW4_PORT, ICW4_FIXED);
	Out8(I8259A_SLAVE_ICW4_PORT, ICW4_FIXED);

	return RETURN_SUCCESS;
}

RETURN_STATUS Enable8259Irq(UINT32 Irq)
{
	if (Irq > 16)
		return RETURN_UNSUPPORTED;

	return RETURN_SUCCESS;
}

RETURN_STATUS Disable8259Irq(UINT32 Irq)
{
	if (Irq > 16)
		return RETURN_UNSUPPORTED;

	return RETURN_SUCCESS;
}

RETURN_STATUS Remapping8259Irq(UINT64 Irq, UINT64 Vector, UINT64 Cpu)
{
	if ((Irq & 0x7) != (Vector & 0x7) || Irq > 16 || Vector > 255 || Cpu != 0)
		return RETURN_UNSUPPORTED;
	
	if (Irq < 8)
		Out8(I8259A_MASTER_ICW2_PORT, Vector & 0xf8);
	else
		Out8(I8259A_SLAVE_ICW2_PORT, Vector & 0xf8);
	
	return RETURN_SUCCESS;
}


