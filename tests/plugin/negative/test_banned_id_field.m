// Negative test: struct fields declared as 'id' must fail compilation
struct BadStruct {
    int header;
    id dynamic_field;
};
