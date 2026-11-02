// D1 link probe: links the whole Core API and nothing else. If any api
// dependency pulled a Qt library, the executable's link line would contain Qt
// and the verification script would flag it.
int main()
{
    return 0;
}
