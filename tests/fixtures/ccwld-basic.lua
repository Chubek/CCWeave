ccwld.output{kind="exe", format="elf", entry="_start"}
ccwld.input("crt.o", "main.o")
ccwld.memory{{name="rom", attrs="rx", origin=4096, length=65536}}
ccwld.sections{ccwld.out(".text", {region="rom", align=16})}
