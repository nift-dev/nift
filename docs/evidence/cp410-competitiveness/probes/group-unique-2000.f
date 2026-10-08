a := inject("/home/nick/Repositories/nift/nift/.build/cp49-campaign/extra/records-2000.json")

b := a.group_by(x => x.v); print(b.size())

