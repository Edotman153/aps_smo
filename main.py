from random import randint
from threading import Thread
from time import sleep

class Request:
    def __init__(self, family):
        self.family = family

class Family(Thread):
    def __init__(self, prio):
        super().__init__()
        self.prio = prio

    def run(self):
        while True:
            a = randint(3, 8)
            sleep(a)
            r = Request(self)
            print(r)



if __name__ == '__main__':
    f = Family(1)
    f.start()
    f.join()
