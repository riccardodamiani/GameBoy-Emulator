file = open("gameboy.cpp", "r")
data = file.read()

print("Implemented " + str(data.count('case')) + " instructions")
input()
