
                AREA    |.text|, CODE, READONLY


; Reset Handler

start_app   PROC
                EXPORT  start_app                
                ; READ val of address(0x08020000), set to SP
                LDR R1, [R0]
                MOV SP, R1
                
                ; READ val of address(0x08020004), jump
                LDR R1, [R0, #4]
                BX R1
                    
                ENDP
                END
                