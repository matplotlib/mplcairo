import unicodedata

from matplotlib import pyplot as plt


# From https://github.com/minimaxir/big-list-of-naughty-strings/
s = """Ṱ̺̺o͞ ̷i̲̬n̝̗v̟̜o̶̙kè͚̮ ̖t̝͕h̼͓e͇̣ ̢̼h͚͎i̦̲v̻͍e̺̭-m̢iͅn̖̺d̵̼ ̞̥r̛̗e͙p͠r̼̞e̺̠s̘͇e͉̥ǹ̬͎t͍̬i̪̱n͠g̴͉ ͏͉c̬̟h͡a̫̻o̫̟s̗̦.̨̹
I̗̘n͇͇v̮̫ok̲̫i̖͙n̡̻g̲͈ ̰t͔̦h̞̲e̢̤ ͍̬f̴̘è͖ẹ̥̩l͖͔i͓͚n͖͍g͍ ̨o͚̪f̘̣ ̖̘c͔̫h̵̤á̗̼o̼̣s̱͈.̛̖
Ṯ̤͍h̲́e͏͓ ͇̜N͕͠e̗̱z̘̝p̤̺e̠̻r̨̤d̠̟i̦͖a̠̗n͚͜ ̻̞h̵͉i̳̞v̢͇ḙ͎͟-̭̩m̤̭i͕͇n̗͙ḍ̟ ̯̲ǫ̟̯f ̪̰c̦͍ḥ͚a̮͎ơ̩̹s̤.̝̝ Z̡̖a͖̰l̲̫g̡̟o̗͜.̟̦
H̬̤e͜ ̜̥w̕h̖̯o̝͙ ̺̙W̷̼a̺̪į͈͕t̶̼s̘͙ ̠̫B̻͍e̵h̵̬i̹͓n͟d̴̪ ̰͉T͖̼h͏͓e̬̝ ̤̹W͙̞a͏͓l̴͔ḽ̫.͕
Z̮̞Ḁ̗̞Ḷ͙͎G̻O̭̗"""
print(s)
for c in s:
    print(unicodedata.name(c, f"?? {ord(c):#x}"))


fig, ax = plt.subplots()
ax.text(.5, .5, s, ha="center", va="center")
ax.set_axis_off()
plt.show()
