# fourth round: list numbers, setext text that starts with marker characters
CASES = [
 ('ordered list 6 to 9', 20, 50, ['x', '6. # six', '7. # seven', '8. ## eight', '9. ### nine', '19. # nineteen', '20. # twenty', '7) # paren', 'y'], ['k:1G']),
 ('ordered list 7 alone', 20, 50, ['7. # seven', 'y'], ['k:2G']),
 ('nested 8', 20, 50, ['- a', '  8. ## eight', 'y'], ['k:3G']),
 ('setext emphasis start', 20, 50, ['x', '', '*1* # b', '===', '', '**-** ## c', '---', 'y'], ['k:1G']),
 ('setext plus start', 20, 50, ['x', '', '+1 # b', '===', '', '1 # c', '---', 'y'], ['k:1G']),
]
