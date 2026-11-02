// P6 link probe: links the whole zero-Qt core and nothing else. If any core
// module pulled a Qt library, the executable's link line would contain Qt and
// the verification script would flag it.
int main()
{
    return 0;
}
