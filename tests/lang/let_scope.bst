let x = 1
{
    let x = 2
    print(x)              # expect: 2
    {
        let x = 3
        print(x)          # expect: 3
    }
    print(x)              # expect: 2
}
print(x)                  # expect: 1
let y = nil
print(y)                  # expect: nil
