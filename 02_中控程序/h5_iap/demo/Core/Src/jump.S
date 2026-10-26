
                AREA    |.text|, CODE, READONLY


; Reset Handler

start_app   PROC
                EXPORT  start_app
                ; SET vector, r0(0x08040000)==>VTOR
                LDR R1, =0xE000ED08
                STR R0, [R1]
                
                ; READ val of address(0x08040000), set to SP
                LDR R1, [R0]
                MOV SP, R1
                
                ; READ val of address(0x08040004), jump
                LDR R1, [R0, #4]
                BX R1
                    
                ENDP
                END