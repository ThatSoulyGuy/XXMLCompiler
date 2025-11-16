	.text
	.def	@feat.00;
	.scl	3;
	.type	0;
	.endef
	.globl	@feat.00
.set @feat.00, 0
	.file	"Phase2Test.ll"
	.def	main;
	.scl	2;
	.type	32;
	.endef
	.globl	main                            # -- Begin function main
	.p2align	4, 0x90
main:                                   # @main
.seh_proc main
# %bb.0:
	subq	$56, %rsp
	.seh_stackalloc 56
	.seh_endprologue
	movl	$10, %ecx
	callq	Integer_Constructor
	movq	%rax, 48(%rsp)
	movl	$5, %ecx
	callq	Integer_Constructor
	movq	%rax, 40(%rsp)
	xorl	%ecx, %ecx
	callq	exit
	xorl	%eax, %eax
	addq	$56, %rsp
	retq
	.seh_endproc
                                        # -- End function
	.addrsig
	.addrsig_sym Integer_Constructor
	.addrsig_sym exit
