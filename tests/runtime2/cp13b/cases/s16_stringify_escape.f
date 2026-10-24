e := error("quote \" backslash \\ tab \t nl \n uni é ctl \u0001")
print(e.stringify())
print(e.prettify())
